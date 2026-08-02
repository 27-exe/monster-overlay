-- mhr-overlay-damage.lua
-- REFramework autorun script: tracks per-player damage and writes JSON
-- to /tmp/mhr_damage.json for the external Qt overlay to consume.
--
-- Install: drop into reframework/autorun/
-- Requires: REFramework (dinput8.dll) for MHRise
--
-- IPC protocol: JSON file at /tmp/mhr_damage.json, refreshed every 30 frames.
-- Schema:
-- {
--   "version": 1,
--   "timestamp": <os.time()>,
--   "quest_active": true/false,
--   "players": [
--     { "slot": 0, "name": "...", "total": 12345.0, "physical": 10000.0,
--       "elemental": 2345.0, "hits": 42, "is_local": true },
--     ...
--   ],
--   "monsters": [
--     { "address": "0x...", "id": 42, "name": "...", "total": 50000.0 }
--   ]
-- }

log.info("[mhr-overlay-damage] loaded")

local OUTPUT_PATH = "/tmp/mhr_damage.json"
local WRITE_INTERVAL = 30  -- frames between file writes

-- Accumulator state
local damage_table = {}   -- keyed by player slot index
local monster_table = {}  -- keyed by monster address
local frame_count = 0
local quest_active = false

-- Type/method references (resolved once)
local enemy_base_type = sdk.find_type_definition("snow.enemy.EnemyCharacterBase")
local enemy_mgr_type = sdk.find_type_definition("snow.enemy.EnemyManager")

-- Hook: snow.enemy.EnemyCharacterBase.setDamage
-- This is called every time damage is applied to a monster.
-- Signature varies by RE engine version; we try multiple method names.
local damage_method = nil
for _, name in ipairs({
    "setDamage",
    "damage",
    "applyDamage",
    "setVitalDamage",
}) do
    local m = enemy_base_type:get_method(name)
    if m then
        damage_method = m
        log.info("[mhr-overlay-damage] hooked: " .. name)
        break
    end
end

if not damage_method then
    -- Fallback: hook via the vital param path (like coavins does)
    log.warn("[mhr-overlay-damage] no direct damage method found, trying vital hook")
    local phys_field = enemy_base_type:get_field("_PhysicalParam")
    if phys_field then
        local phys_type = phys_field:get_type()
        local vital_method = phys_type:get_method("getVital")
        if vital_method then
            local vital_type = vital_method:get_return_type()
            damage_method = vital_type:get_method("set_Current")
            if damage_method then
                log.info("[mhr-overlay-damage] hooked: VitalParam.set_Current")
            end
        end
    end
end

-- Player identification
local function get_player_name(slot)
    local players = sdk.get_managed_singleton("snow.player.PlayerManager")
    if not players then return "Player " .. slot end
    local player_list = players:get_field("_Players")
    if not player_list then return "Player " .. slot end
    local elements = player_list:get_elements()
    if not elements or not elements[slot + 1] then return "Player " .. slot end
    local player = elements[slot + 1]
    local name = player:call("get_PlayerName")
    return name or ("Player " .. slot)
end

-- Damage hook implementation
local function on_damage_pre(args)
    -- args[2] = this (monster), args[3..] = damage params depending on method
    local monster = sdk.to_managed_object(args[2])
    if not monster then return end

    -- Extract damage value from args (position depends on hooked method)
    local raw_dmg = 0.0
    local elem_dmg = 0.0
    local attacker_slot = 0

    -- Try to read as DamageInfo structure
    local dmg_info = sdk.to_managed_object(args[3])
    if dmg_info then
        raw_dmg = sdk.to_float(dmg_info:call("get_Raw") or 0)
        elem_dmg = sdk.to_float(dmg_info:call("get_Elemental") or 0)
        local atk_type = dmg_info:call("get_AttackerType") or 0
        -- 0 = player 0, 1 = player 1, ..., 0x15-0x17 = pet
        if atk_type >= 0 and atk_type <= 3 then
            attacker_slot = atk_type
        elseif atk_type >= 0x15 and atk_type <= 0x17 then
            attacker_slot = atk_type  -- pets tracked separately
        end
    else
        -- Fallback: args[3] might be raw float damage
        raw_dmg = sdk.to_float(args[3] or 0)
    end

    local total = raw_dmg + elem_dmg
    if total <= 0 then return end

    -- Accumulate player damage
    local entry = damage_table[attacker_slot]
    if not entry then
        entry = {
            slot = attacker_slot,
            name = get_player_name(attacker_slot),
            total = 0.0,
            physical = 0.0,
            elemental = 0.0,
            hits = 0,
            is_local = (attacker_slot == 0),
        }
        damage_table[attacker_slot] = entry
    end
    entry.total = entry.total + total
    entry.physical = entry.physical + raw_dmg
    entry.elemental = entry.elemental + elem_dmg
    entry.hits = entry.hits + 1

    -- Track monster
    local addr = string.format("0x%X", sdk.to_uint64(args[2]))
    local mon = monster_table[addr]
    if not mon then
        local mon_id = 0
        local id_field = enemy_base_type:get_field("_EnemyDef")
        if id_field then
            local enemy_def = id_field:get_data(monster)
            if enemy_def then
                mon_id = enemy_def:get_field("_EnemyType") or 0
            end
        end
        mon = { address = addr, id = mon_id, name = "Monster #" .. mon_id, total = 0.0 }
        monster_table[addr] = mon
    end
    mon.total = mon.total + total

    quest_active = true
end

-- Install hook
if damage_method then
    sdk.hook(damage_method, on_damage_pre, function(retval) return retval end)
else
    log.error("[mhr-overlay-damage] FATAL: no damage hook method found")
end

-- Quest state detection: reset accumulators when quest ends/starts
re.on_pre_application_entry("UpdateBehavior", function()
    frame_count = frame_count + 1

    -- Detect quest state via GUI manager
    local gui = sdk.get_managed_singleton("snow.gui.GuiManager")
    if gui then
        local quest_mgr = gui:get_field("_QuestManager")
        if quest_mgr then
            local state = quest_mgr:call("get_QuestState") or 0
            local in_quest = (state == 2)  -- InQuest = 2
            if quest_active and not in_quest then
                -- Quest just ended — keep final data but mark inactive
                quest_active = false
            elseif not quest_active and in_quest then
                -- New quest started — reset
                damage_table = {}
                monster_table = {}
                quest_active = true
            end
        end
    end
end)

-- Write JSON output periodically
re.on_frame(function()
    if frame_count % WRITE_INTERVAL ~= 0 then return end

    -- Build players array
    local players_json = {}
    for slot, entry in pairs(damage_table) do
        table.insert(players_json, string.format(
            '{"slot":%d,"name":"%s","total":%.1f,"physical":%.1f,"elemental":%.1f,"hits":%d,"is_local":%s}',
            entry.slot,
            entry.name:gsub('"', '\\"'),
            entry.total,
            entry.physical,
            entry.elemental,
            entry.hits,
            entry.is_local and "true" or "false"
        ))
    end

    -- Build monsters array
    local monsters_json = {}
    for addr, mon in pairs(monster_table) do
        table.insert(monsters_json, string.format(
            '{"address":"%s","id":%d,"name":"%s","total":%.1f}',
            mon.address,
            mon.id,
            mon.name:gsub('"', '\\"'),
            mon.total
        ))
    end

    local json_str = string.format(
        '{"version":1,"timestamp":%d,"quest_active":%s,"players":[%s],"monsters":[%s]}',
        os.time(),
        quest_active and "true" or "false",
        table.concat(players_json, ","),
        table.concat(monsters_json, ",")
    )

    -- Write atomically: write to temp then rename
    local tmp_path = OUTPUT_PATH .. ".tmp"
    local f = io.open(tmp_path, "w")
    if f then
        f:write(json_str)
        f:close()
        os.rename(tmp_path, OUTPUT_PATH)
    end
end)

-- Cleanup on script unload
re.on_script_reset(function()
    os.remove(OUTPUT_PATH)
    log.info("[mhr-overlay-damage] unloaded, removed " .. OUTPUT_PATH)
end)
