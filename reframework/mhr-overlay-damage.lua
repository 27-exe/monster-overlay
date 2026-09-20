-- mhr-overlay-damage.lua
-- REFramework autorun script: tracks verified player/companion/pet damage and publishes
-- an entity-oriented JSON snapshot for the external Qt overlay.
--
-- Install: drop into reframework/autorun/
-- Requires: REFramework (dinput8.dll) for Monster Hunter Rise
--
-- IPC protocol: reframework/data/mhr_damage_{a,b}.json, refreshed every 30
-- frames. REFramework 1.5.9.1 sandboxes Lua filesystem access to this data
-- directory. Alternating slots retain one complete snapshot while the other
-- is truncated and rewritten by fs.write().
-- Schema v2 (abbreviated):
-- {
--   "version": 2,
--   "schema": "mhr-overlay-damage/v2",
--   "seq": 1,
--   "timestamp_ms": 1700000000000,
--   "pet_aggregation": "per_owner",
--   "quest": { "active": true, "state": 2, "epoch": 1,
--              "state_valid": true, "collection_paused": false,
--              "training": false },
--   "entities": [
--     { "key": "player:0", "kind": "player", "entity_index": 0,
--       "display_slot": -1, "owner_entity_index": -1,
--       "source_type_raw": 0, "name": "...",
--       "owner_name": "", "total": 123.0, "physical": 100.0,
--       "elemental": 23.0, "hits": 4, "is_local": false }
--   ],
--   "diagnostics": { ... }
-- }

log.info("[mhr-overlay-damage] loaded (schema v2)")

local OUTPUT_PATH = "/tmp/mhr_damage.json" -- legacy fallback path
local OUTPUT_SLOT_A = "mhr_damage_a.json"
local OUTPUT_SLOT_B = "mhr_damage_b.json"
local WRITE_INTERVAL = 30
local SCHEMA_NAME = "mhr-overlay-damage/v2"
local MAX_ENTITY_INDEX = 9 -- HunterPie's HuntStatistics entity array has 10 slots.
local HUNTERPIE_TARGET_ID_OFFSET = 0x2D4 -- Games::Rise::Common::Monster::id

-- HunterPie-verified attacker semantics. Pet damage is one aggregate row per
-- owner: 0x15..0x17 are not separate buddies. The upstream damage Id cannot
-- distinguish an owner's second buddy, so this producer must not invent one or
-- infer palico/palamute species from those values.
local PLAYER_SOURCE_TYPE = 0
local PET_SOURCE_TYPES = {
    [0x15] = true,
    [0x16] = true,
    [0x17] = true,
}

-- Accessor candidates are deliberately explicit. A matching attacker type is
-- never used as an entity slot: Id is read independently from the same payload.
local SOURCE_TYPE_METHODS = {
    "get_DamageAttackerType",
    "get_AttackerDamageType",
    "get_AttackerType",
}
local SOURCE_TYPE_FIELDS = {
    "damageAttackerType",
    "DamageAttackerType",
    "_DamageAttackerType",
    "<DamageAttackerType>k__BackingField",
    "attackerDamageType",
    "AttackerDamageType",
    "_AttackerDamageType",
    "_attackerDamageType",
    "<AttackerDamageType>k__BackingField",
    "attackerType",
    "AttackerType",
    "_AttackerType",
}
local ENTITY_ID_METHODS = {
    "get_AttackerID",
    "get_AttackerId",
    "get_Id",
    "get_ID",
    "get_EntityId",
    "get_EntityID",
    "get_EntityIndex",
}
local ENTITY_ID_FIELDS = {
    "AttackerID",
    "_AttackerID",
    "attackerID",
    "AttackerId",
    "_AttackerId",
    "attackerId",
    "Id",
    "_Id",
    "id",
    "_id",
    "<Id>k__BackingField",
    "EntityId",
    "_EntityId",
    "entityId",
    "EntityIndex",
    "_EntityIndex",
}
local RAW_DAMAGE_METHODS = {
    "get_PhysicalDamage",
    "get_RawDamage",
    "get_Raw",
    "get_Physical",
}
local RAW_DAMAGE_FIELDS = {
    "rawDamage",
    "RawDamage",
    "_RawDamage",
    "Raw",
    "_Raw",
    "physicalDamage",
    "PhysicalDamage",
    "_PhysicalDamage",
}
local ELEMENTAL_DAMAGE_METHODS = {
    "get_ElementDamage",
    "get_ElementalDamage",
    "get_Elemental",
}
local ELEMENTAL_DAMAGE_FIELDS = {
    "elementDamage",
    "ElementDamage",
    "_ElementDamage",
    "elementalDamage",
    "ElementalDamage",
    "_ElementalDamage",
    "Elemental",
    "_Elemental",
}

local ENEMY_DEF_METHODS = {
    "get_EnemyDef",
    "get_EnemyDefinition",
}
local ENEMY_DEF_FIELDS = {
    "_EnemyDef",
    "EnemyDef",
    "_EnemyDefinition",
    "EnemyDefinition",
}
local ENEMY_ID_METHODS = {
    "get_EnemyType",
    "get_EnemyTypeIndex",
    "get_EmType",
}
local ENEMY_ID_FIELDS = {
    "_EnemyType",
    "EnemyType",
    "enemyType",
    "_EnemyTypeIndex",
    "EnemyTypeIndex",
    "_EmType",
    "EmType",
}

local QUEST_MANAGER_METHODS = {
    "get_QuestManager",
}
local QUEST_MANAGER_FIELDS = {
    "_QuestManager",
    "QuestManager",
    "<QuestManager>k__BackingField",
}
local QUEST_STATE_METHODS = {
    "get_QuestStatus",
    "getQuestStatus",
    "get_QuestState",
    "getQuestState",
}
local QUEST_STATE_FIELDS = {
    -- _QuestStatus is the field used by maintained Rise meters and matches
    -- HunterPie's QUEST_STATUS_OFFSETS terminology.
    "_QuestStatus",
    "QuestStatus",
    "_questStatus",
    "<QuestStatus>k__BackingField",
    "_QuestState",
    "QuestState",
    "_questState",
    "<QuestState>k__BackingField",
}

-- Both candidates are used by established Rise overlays. The method is the
-- direct game predicate; the exact area number is HunterPie's StageId == 5
-- training-room exception (MHRStageStructure Type 4 / VillageId 5). No broad
-- "village" or non-quest heuristic is accepted, so the lobby cannot become a
-- false training session.
local TRAINING_ROOM_METHODS = {
    "checkCurrentArea_TrainingArea",
}
local VILLAGE_AREA_METHODS = {
    "get_CurrentAreaNo",
    "getCurrentAreaNo",
}
local VILLAGE_AREA_FIELDS = {
    "<_CurrentAreaNo>k__BackingField",
    "_CurrentAreaNo",
    "CurrentAreaNo",
}
local TRAINING_ROOM_AREA_ID = 5

local PLAYER_NAME_METHODS = {
    "get_PlayerName",
    "get_Name",
}
local PLAYER_NAME_FIELDS = {
    "_PlayerName",
    "PlayerName",
    "_Name",
    "Name",
}

local function safe_call(receiver, method_name, ...)
    if not receiver then return nil end
    local lookup_ok, fn = pcall(function() return receiver[method_name] end)
    if not lookup_ok then return nil end
    if type(fn) ~= "function" then return nil end
    local ok, value = pcall(fn, receiver, ...)
    if not ok then return nil end
    return value
end

local function safe_type_definition(object)
    return safe_call(object, "get_type_definition")
end

local function read_candidate(object, method_names, field_names)
    if not object then return nil, nil end

    local type_definition = safe_type_definition(object)
    if type_definition then
        for _, name in ipairs(method_names) do
            local method = safe_call(type_definition, "get_method", name)
            if method then
                local value = safe_call(method, "call", object)
                if value ~= nil then
                    return value, "method:" .. name
                end
            end
        end

        for _, name in ipairs(field_names) do
            local field = safe_call(type_definition, "get_field", name)
            if field then
                local value = safe_call(field, "get_data", object)
                if value ~= nil then
                    return value, "field:" .. name
                end
            end
        end
    end

    -- Older REFramework builds, inherited TDB members, and some value types can
    -- expose object access even when type-definition lookup misses the member.
    -- Keep this guarded fallback limited to the same explicit candidate names.
    for _, name in ipairs(method_names) do
        local ok, value = pcall(function() return object:call(name) end)
        if ok and value ~= nil then
            return value, "method:" .. name
        end
    end
    for _, name in ipairs(field_names) do
        local ok, value = pcall(function() return object:get_field(name) end)
        if ok and value ~= nil then
            return value, "field:" .. name
        end
    end

    return nil, nil
end

local function finite_number(value, converter_name)
    if value == nil then return nil end
    local number = tonumber(value)
    if number == nil and converter_name and sdk[converter_name] then
        local ok, converted = pcall(sdk[converter_name], value)
        if ok then number = tonumber(converted) end
    end
    if number == nil or number ~= number or number == math.huge or number == -math.huge then
        return nil
    end
    return number
end

local function integer_value(value)
    local number = finite_number(value, "to_int64")
    if number == nil or number ~= math.floor(number) then return nil end
    return number
end

local function boolean_value(value)
    if type(value) == "boolean" then return value end
    local number = integer_value(value)
    if number == 0 then return false end
    if number == 1 then return true end
    return nil
end

local function float_value(value)
    return finite_number(value, "to_float") or finite_number(value, "to_double")
end

local function read_integer(object, method_names, field_names)
    local value, accessor = read_candidate(object, method_names, field_names)
    return integer_value(value), accessor
end

local function read_boolean(object, method_names, field_names)
    local value, accessor = read_candidate(object, method_names, field_names)
    return boolean_value(value), accessor
end

local function read_float(object, method_names, field_names)
    local value, accessor = read_candidate(object, method_names, field_names)
    return float_value(value), accessor
end

local function to_managed_object(value)
    if value == nil or not sdk.to_managed_object then return nil end
    local ok, object = pcall(sdk.to_managed_object, value)
    if not ok then return nil end
    return object
end

local function managed_singleton(name)
    if not sdk.get_managed_singleton then return nil end
    local ok, singleton = pcall(sdk.get_managed_singleton, name)
    if not ok then return nil end
    return singleton
end

local function find_type(name)
    if not sdk.find_type_definition then return nil end
    local ok, type_definition = pcall(sdk.find_type_definition, name)
    if not ok then return nil end
    return type_definition
end

local function new_diagnostics()
    return {
        dropped_unknown_events = 0,
        ambiguous_pet_events = 0,
        missing_identity_events = 0,
        missing_damage_payload_events = 0,
        unconfirmed_target_events = 0,
        filtered_non_big_target_events = 0,
        target_id_native_reads = 0,
        target_id_managed_reads = 0,
        invalid_damage_events = 0,
        quest_state_read_failures = 0,
        training_state_read_failures = 0,
        training_state_conflicts = 0,
        mixed_pet_source_events = 0,
        unknown_attacker_types = {},
        dropped_source_types = {},
        unknown_attacker_targets = {},
        pet_entity_events = {},
        pet_entity_damage = {},
        filtered_target_ids = {},
    }
end

local entities = {}
local diagnostics = new_diagnostics()
local write_failures = 0
local last_publish_api = ""
local sequence = 0
local write_frame_count = 0
local quest_active = false
local quest_state = 0
local quest_epoch = 0
local quest_state_valid = false
local collection_paused = true
local quest_training = false
local collection_mode = "none"
local training_state_valid = false
local training_detector = ""

local function increment_count(map, key)
    if key == nil then return end
    map[key] = (map[key] or 0) + 1
end

local function record_unknown_source(raw_type, target_id)
    diagnostics.dropped_unknown_events = diagnostics.dropped_unknown_events + 1
    if raw_type == nil then
        diagnostics.missing_identity_events = diagnostics.missing_identity_events + 1
        return
    end
    increment_count(diagnostics.unknown_attacker_types, raw_type)
    increment_count(diagnostics.dropped_source_types, raw_type)
    if target_id ~= nil then
        -- Pair each excluded source with the monster it hit so a live
        -- session can identify what an unknown attacker type actually is
        -- (monster brawls, terrain, bombs, riding).
        increment_count(diagnostics.unknown_attacker_targets,
            tostring(raw_type) .. "->" .. tostring(target_id))
    end
end

local function record_missing_identity(raw_type)
    diagnostics.dropped_unknown_events = diagnostics.dropped_unknown_events + 1
    diagnostics.missing_identity_events = diagnostics.missing_identity_events + 1
    if raw_type == nil then return end
    increment_count(diagnostics.dropped_source_types, raw_type)
    if raw_type ~= PLAYER_SOURCE_TYPE and not PET_SOURCE_TYPES[raw_type] then
        increment_count(diagnostics.unknown_attacker_types, raw_type)
    end
end

-- Exact HunterPie Rise big-monster predicate expressed as ranges. Vanilla IDs
-- are 0..46. Sunbreak's mask contains 76..98 and 107..115.
local function is_big_monster_id(enemy_id)
    return (enemy_id >= 0 and enemy_id <= 46)
        or (enemy_id >= 76 and enemy_id <= 98)
        or (enemy_id >= 107 and enemy_id <= 115)
end

local function get_enemy_id(monster)
    local enemy_def = read_candidate(monster, ENEMY_DEF_METHODS, ENEMY_DEF_FIELDS)
    if enemy_def ~= nil then
        -- Some revisions expose EnemyDef itself as a numeric enum. Never feed
        -- a managed EnemyDef object through an integer pointer converter.
        if type(enemy_def) == "number" then
            local direct_id = integer_value(enemy_def)
            if direct_id ~= nil then return direct_id end
        end

        local enemy_id = read_integer(enemy_def, ENEMY_ID_METHODS, ENEMY_ID_FIELDS)
        if enemy_id ~= nil then return enemy_id end
    end

    -- Some TDB revisions expose EnemyType directly on EnemyCharacterBase.
    return read_integer(monster, ENEMY_ID_METHODS, ENEMY_ID_FIELDS)
end

local function get_native_enemy_id(raw_target)
    -- HunterPie's native Rise hook reads target->id at +0x2D4. The managed
    -- get_EnemyType accessor is not equivalent on the current TDB and yielded
    -- values outside the verified large-monster mask.
    if raw_target == nil or not sdk.to_int64 or not sdk.to_valuetype then return nil end
    local base_ok, base = pcall(sdk.to_int64, raw_target)
    if not base_ok or type(base) ~= "number" or base == 0 then return nil end
    local value_ok, value_type = pcall(
        sdk.to_valuetype,
        base + HUNTERPIE_TARGET_ID_OFFSET,
        "System.Int32"
    )
    if not value_ok or value_type == nil then return nil end
    local field_ok, value = pcall(function()
        return value_type:get_field("mValue")
    end)
    if not field_ok then return nil end
    return integer_value(value)
end

local function get_player_name(entity_id)
    if entity_id < 0 then return nil end
    local player_manager = managed_singleton("snow.player.PlayerManager")
    if not player_manager then return nil end

    local player_list = read_candidate(player_manager, {}, {
        "_Players",
        "Players",
        "_PlayerList",
        "PlayerList",
    })
    if not player_list then return nil end

    local elements = safe_call(player_list, "get_elements")
    if type(elements) ~= "table" then return nil end

    -- REFramework's get_elements() convention is a one-based Lua array.
    local player = elements[entity_id + 1]
    if not player then return nil end

    local name = read_candidate(player, PLAYER_NAME_METHODS, PLAYER_NAME_FIELDS)
    if type(name) ~= "string" or name == "" then return nil end
    return name
end

local function extract_damage_payload(args)
    local best_raw_type = nil
    local saw_damage_payload = false

    -- Managed method signatures have changed between TDB revisions. Search a
    -- small, explicit argument window, but require damage + type + Id to come
    -- from the same managed object.
    for argument_index = 3, 8 do
        local object = to_managed_object(args[argument_index])
        if object then
            local raw_damage, raw_accessor = read_float(
                object,
                RAW_DAMAGE_METHODS,
                RAW_DAMAGE_FIELDS
            )
            local elemental_damage, elemental_accessor = read_float(
                object,
                ELEMENTAL_DAMAGE_METHODS,
                ELEMENTAL_DAMAGE_FIELDS
            )

            if raw_accessor or elemental_accessor then
                saw_damage_payload = true
                local source_type, source_accessor = read_integer(
                    object,
                    SOURCE_TYPE_METHODS,
                    SOURCE_TYPE_FIELDS
                )
                local entity_id, id_accessor = read_integer(
                    object,
                    ENTITY_ID_METHODS,
                    ENTITY_ID_FIELDS
                )

                if source_type ~= nil then best_raw_type = source_type end
                if source_accessor and id_accessor
                    and source_type ~= nil and entity_id ~= nil
                    and (raw_damage ~= nil or elemental_damage ~= nil) then
                    return {
                        raw_damage = raw_damage or 0.0,
                        elemental_damage = elemental_damage or 0.0,
                        source_type_raw = source_type,
                        entity_id = entity_id,
                    }, nil
                end
            end
        end
    end

    return nil, {
        source_type_raw = best_raw_type,
        saw_damage_payload = saw_damage_payload,
    }
end

local function classify_actor(source_type_raw, entity_id)
    if source_type_raw == PLAYER_SOURCE_TYPE then
        if entity_id < 0 or entity_id > MAX_ENTITY_INDEX then
            return nil, "invalid_player_entity"
        end

        -- HunterPie's source type 0 shares one Id space: 0..3 are hunters and
        -- 4..9 are companion entities. Id is an identity key, not a UI slot.
        local kind = entity_id <= 3 and "player" or "companion"
        return {
            key = kind .. ":" .. string.format("%d", entity_id),
            kind = kind,
            entity_index = entity_id,
            display_slot = -1,
            owner_entity_index = -1,
            -- Only the C++ roster join can establish local/display identity.
            local_actor = false,
        }, nil
    end

    if PET_SOURCE_TYPES[source_type_raw] then
        -- Live sessions decode the raw pet ids as:
        --   0..3 owner-index form (a hunter's first buddy; +5 for the
        --        historical canonical slot)
        --   4    the local hunter's second buddy
        --   5, 6 follower buddies of follower slot 0 / 1 (present only while
        --        followers are in the party; both carry chip damage)
        -- Only the C++ roster layer can see whether followers exist, so raw
        -- ids 4..9 keep their identity here and are re-attributed there.
        if entity_id >= 0 and entity_id <= 3 then
            return {
                key = "pet:" .. string.format("%d", entity_id),
                kind = "pet",
                entity_index = entity_id + 5,
                display_slot = -1,
                owner_entity_index = entity_id,
                -- The roster join decides local/display identity.
                local_actor = false,
            }, nil
        end
        if entity_id >= 4 and entity_id <= 9 then
            return {
                key = "pet-entity:" .. string.format("%d", entity_id),
                kind = "pet",
                entity_index = entity_id,
                display_slot = -1,
                owner_entity_index = -1,
                local_actor = false,
            }, nil
        end
        return nil, "ambiguous_pet_owner"
    end

    return nil, "unknown_source_type"
end

local function accumulate_damage(actor, source_type_raw, raw_damage, elemental_damage)
    local entry = entities[actor.key]
    local physical = (entry and entry.physical or 0.0) + raw_damage
    local elemental = (entry and entry.elemental or 0.0) + elemental_damage
    local total = (entry and entry.total or 0.0) + raw_damage + elemental_damage
    if physical ~= physical or elemental ~= elemental or total ~= total
        or physical == math.huge or elemental == math.huge or total == math.huge then
        return false
    end
    if entry and math.maxinteger and entry.hits >= math.maxinteger then return false end

    if not entry then
        entry = {
            key = actor.key,
            kind = actor.kind,
            entity_index = actor.entity_index,
            display_slot = actor.display_slot,
            owner_entity_index = actor.owner_entity_index,
            source_type_raw = source_type_raw,
            source_types_raw = { [source_type_raw] = true },
            name = "",
            owner_name = "",
            total = 0.0,
            physical = 0.0,
            elemental = 0.0,
            hits = 0,
            local_actor = actor.local_actor,
        }
        entities[actor.key] = entry
    elseif not entry.source_types_raw[source_type_raw] then
        entry.source_types_raw[source_type_raw] = true
        diagnostics.mixed_pet_source_events = diagnostics.mixed_pet_source_events + 1
        -- Keep the scalar compatibility field deterministic while preserving
        -- every observed value in source_types_raw.
        if source_type_raw < entry.source_type_raw then
            entry.source_type_raw = source_type_raw
        end
    end

    entry.physical = physical
    entry.elemental = elemental
    entry.total = total
    entry.hits = entry.hits + 1
    return true
end

local function on_damage_pre(args)
    -- Collect only in a confirmed formal quest or training-room epoch. Result
    -- states remain frozen, and any transient state read failure pauses writes
    -- without changing the last confirmed lifecycle state.
    if not quest_active or collection_paused then return end

    local monster = to_managed_object(args[2])
    if not monster then
        diagnostics.unconfirmed_target_events = diagnostics.unconfirmed_target_events + 1
        return
    end

    local enemy_id = get_native_enemy_id(args[2])
    if enemy_id ~= nil then
        diagnostics.target_id_native_reads = diagnostics.target_id_native_reads + 1
    else
        enemy_id = get_enemy_id(monster)
        if enemy_id ~= nil then
            diagnostics.target_id_managed_reads = diagnostics.target_id_managed_reads + 1
        end
    end
    if enemy_id == nil then
        diagnostics.unconfirmed_target_events = diagnostics.unconfirmed_target_events + 1
        return
    end
    if not is_big_monster_id(enemy_id) then
        diagnostics.filtered_non_big_target_events = diagnostics.filtered_non_big_target_events + 1
        increment_count(diagnostics.filtered_target_ids, enemy_id)
        return
    end

    local payload, missing = extract_damage_payload(args)
    if not payload then
        diagnostics.missing_damage_payload_events = diagnostics.missing_damage_payload_events + 1
        -- A managed payload without Id and a raw-float argument both lack an
        -- independently verified identity. Neither contributes formal damage.
        record_missing_identity(missing and missing.source_type_raw or nil)
        return
    end

    local raw_damage = payload.raw_damage
    local elemental_damage = payload.elemental_damage
    local total = raw_damage + elemental_damage
    if raw_damage < 0 or elemental_damage < 0 or total <= 0
        or total ~= total or total == math.huge then
        diagnostics.invalid_damage_events = diagnostics.invalid_damage_events + 1
        return
    end

    if PET_SOURCE_TYPES[payload.source_type_raw] then
        -- Which raw entity ids the game reports in this layout and how much
        -- damage each stream carries. Together with each stream's per-hit
        -- profile these close the solo/ally/multiplayer ownership questions
        -- from real sessions without guessing owners.
        increment_count(diagnostics.pet_entity_events, payload.entity_id)
        local pet_damage = diagnostics.pet_entity_damage
        pet_damage[payload.entity_id] = (pet_damage[payload.entity_id] or 0) + total
    end

    local actor, reason = classify_actor(payload.source_type_raw, payload.entity_id)
    if not actor then
        if reason == "ambiguous_pet_owner" then
            diagnostics.ambiguous_pet_events = diagnostics.ambiguous_pet_events + 1
            diagnostics.dropped_unknown_events = diagnostics.dropped_unknown_events + 1
            increment_count(diagnostics.dropped_source_types, payload.source_type_raw)
        elseif reason == "invalid_player_entity" then
            record_missing_identity(payload.source_type_raw)
        else
            record_unknown_source(payload.source_type_raw, enemy_id)
        end
        return
    end

    if not accumulate_damage(
        actor,
        payload.source_type_raw,
        raw_damage,
        elemental_damage
    ) then
        diagnostics.invalid_damage_events = diagnostics.invalid_damage_events + 1
    end
end

local function get_quest_state()
    local quest_manager = nil
    local gui_manager = managed_singleton("snow.gui.GuiManager")
    if gui_manager then
        quest_manager = read_candidate(
            gui_manager,
            QUEST_MANAGER_METHODS,
            QUEST_MANAGER_FIELDS
        )
    end
    if not quest_manager then
        quest_manager = managed_singleton("snow.QuestManager")
    end
    if not quest_manager then return nil end

    return read_integer(
        quest_manager,
        QUEST_STATE_METHODS,
        QUEST_STATE_FIELDS
    )
end

local function get_training_state()
    -- VillageAreaManager references may be absent or stale across area loads;
    -- reacquire the singleton for every probe, as Coavins' maintained meter does.
    local area_manager = managed_singleton("snow.VillageAreaManager")
    if not area_manager then return nil, nil, "manager_unavailable" end

    local predicate, predicate_accessor = read_boolean(
        area_manager,
        TRAINING_ROOM_METHODS,
        {}
    )
    local area_id, area_accessor = read_integer(
        area_manager,
        VILLAGE_AREA_METHODS,
        VILLAGE_AREA_FIELDS
    )
    local area_says_training = nil
    if area_id ~= nil then
        area_says_training = (area_id == TRAINING_ROOM_AREA_ID)
    end

    -- If two trusted candidates disagree during a transition, do not guess.
    if predicate ~= nil and area_says_training ~= nil
        and predicate ~= area_says_training then
        return nil,
            (predicate_accessor or "predicate") .. "/" .. (area_accessor or "area"),
            "candidate_conflict"
    end
    if predicate ~= nil then return predicate, predicate_accessor, nil end
    if area_says_training ~= nil then return area_says_training, area_accessor, nil end
    return nil, nil, "no_supported_accessor"
end

local function enter_collection_mode(next_mode)
    if next_mode ~= "none" and next_mode ~= collection_mode then
        entities = {}
        diagnostics = new_diagnostics()
        quest_epoch = quest_epoch + 1
    end

    collection_mode = next_mode
    quest_active = (next_mode ~= "none")
    quest_training = (next_mode == "training")
end

local function update_quest_state()
    local state = get_quest_state()
    if state == nil then
        -- Preserve the complete last confirmed lifecycle tuple. In particular,
        -- active must not become false: the consumer uses state_valid/paused to
        -- keep the previous frame while this producer stops accumulation.
        quest_state_valid = false
        collection_paused = true
        diagnostics.quest_state_read_failures = diagnostics.quest_state_read_failures + 1
        if diagnostics.quest_state_read_failures <= 3
            or diagnostics.quest_state_read_failures % 600 == 0 then
            log.warn("[mhr-overlay-damage] quest state unavailable; collection is paused")
        end
        return
    end

    quest_state = state
    quest_state_valid = true
    collection_paused = false

    local next_mode = "none"
    local mode_resolved = true
    if state == 2 then
        -- Formal quest collection never depends on a village-area probe.
        next_mode = "quest"
        training_state_valid = true
        training_detector = "not_applicable:formal_quest"
    elseif state == 0 or state == 1 then
        local training, detector, failure = get_training_state()
        if training == nil then
            -- Keep exact QuestState behaviour when training cannot be proven.
            -- This intentionally becomes inactive rather than treating an
            -- ordinary lobby as training. The last confirmed training mode is
            -- retained only as an internal epoch boundary, so a one-frame
            -- detector gap does not erase totals on recovery.
            mode_resolved = false
            training_state_valid = false
            training_detector = detector or ""
            diagnostics.training_state_read_failures =
                diagnostics.training_state_read_failures + 1
            if failure == "candidate_conflict" then
                diagnostics.training_state_conflicts =
                    diagnostics.training_state_conflicts + 1
            end
            if diagnostics.training_state_read_failures <= 3
                or diagnostics.training_state_read_failures % 600 == 0 then
                log.warn("[mhr-overlay-damage] training room unavailable ("
                    .. tostring(failure) .. "); formal quest logic retained")
            end
        else
            training_state_valid = true
            training_detector = detector or ""
            if training then next_mode = "training" end
        end
    else
        -- Confirmed result/end states are neither formal collection nor
        -- training. Final totals remain in the snapshot until the next epoch.
        training_state_valid = true
        training_detector = "not_applicable:quest_result"
    end

    if mode_resolved then
        enter_collection_mode(next_mode)
    elseif collection_mode == "training" then
        -- Hide/pause an unconfirmed training session without declaring an
        -- internal exit; a later confirmed true therefore resumes the same
        -- epoch, while a confirmed false closes it.
        quest_active = false
        quest_training = false
    else
        -- A confirmed non-quest QuestState ends a formal quest even when the
        -- optional training probe is unavailable.
        enter_collection_mode("none")
    end
end

local JSON_ESCAPES = {
    ['"'] = '\\"',
    ['\\'] = '\\\\',
    ['\b'] = '\\b',
    ['\f'] = '\\f',
    ['\n'] = '\\n',
    ['\r'] = '\\r',
    ['\t'] = '\\t',
}

local function json_escape(value)
    local text = tostring(value or "")
    return (text:gsub('[%z\1-\31\\"]', function(character)
        return JSON_ESCAPES[character]
            or string.format("\\u%04X", string.byte(character))
    end))
end

local function json_string(value)
    return '"' .. json_escape(value) .. '"'
end

local function json_boolean(value)
    return value and "true" or "false"
end

local function json_integer(value)
    return string.format("%d", value)
end

local function json_number(value)
    if type(value) ~= "number" or value ~= value
        or value == math.huge or value == -math.huge then
        return "0"
    end
    if value == 0 then return "0" end -- also normalizes negative zero
    local encoded = string.format("%.6f", value)
    -- Guard against a non-C numeric locale in the host process.
    encoded = encoded:gsub(",", ".")
    encoded = encoded:gsub("(%..-)0+$", "%1")
    encoded = encoded:gsub("%.$", "")
    return encoded
end

local function sorted_numeric_keys(map)
    local keys = {}
    for key in pairs(map) do table.insert(keys, key) end
    table.sort(keys, function(left, right)
        local left_number = tonumber(left)
        local right_number = tonumber(right)
        if left_number and right_number and left_number ~= right_number then
            return left_number < right_number
        end
        return tostring(left) < tostring(right)
    end)
    return keys
end

local function encode_count_map(map)
    local members = {}
    for _, key in ipairs(sorted_numeric_keys(map)) do
        table.insert(members,
            json_string(tostring(key)) .. ":" .. json_integer(map[key]))
    end
    return "{" .. table.concat(members, ",") .. "}"
end

local function encode_source_types(entry)
    local values = {}
    for _, source_type in ipairs(sorted_numeric_keys(entry.source_types_raw)) do
        table.insert(values, json_integer(source_type))
    end
    return "[" .. table.concat(values, ",") .. "]"
end

local function sorted_entities()
    local result = {}
    for _, entry in pairs(entities) do table.insert(result, entry) end
    table.sort(result, function(left, right)
        if left.entity_index ~= right.entity_index then
            return left.entity_index < right.entity_index
        end
        if left.kind ~= right.kind then return left.kind < right.kind end
        return left.key < right.key
    end)
    return result
end

local function encode_entity(entry)
    if entry.kind == "player" then
        entry.name = get_player_name(entry.entity_index) or entry.name
        entry.owner_name = ""
    else
        entry.owner_name = get_player_name(entry.owner_entity_index) or entry.owner_name
    end

    return table.concat({
        "{",
        "\"key\":", json_string(entry.key),
        ",\"kind\":", json_string(entry.kind),
        ",\"entity_index\":", json_integer(entry.entity_index),
        ",\"display_slot\":", json_integer(entry.display_slot),
        ",\"owner_entity_index\":", json_integer(entry.owner_entity_index),
        ",\"source_type_raw\":", json_integer(entry.source_type_raw),
        ",\"source_types_raw\":", encode_source_types(entry),
        ",\"name\":", json_string(entry.name),
        ",\"owner_name\":", json_string(entry.owner_name),
        ",\"total\":", json_number(entry.total),
        ",\"physical\":", json_number(entry.physical),
        ",\"elemental\":", json_number(entry.elemental),
        ",\"hits\":", json_integer(entry.hits),
        ",\"is_local\":", json_boolean(entry.local_actor),
        "}",
    })
end

local application_type = find_type("via.Application")
local uptime_method = nil
if application_type then
    for _, name in ipairs({ "get_UpTimeSecond", "get_UpTimeSeconds" }) do
        uptime_method = safe_call(application_type, "get_method", name)
        if uptime_method then break end
    end
end

local function uptime_seconds()
    if not uptime_method then return nil end
    local value = safe_call(uptime_method, "call", nil)
    return finite_number(value, nil)
end

local CLOCK_REANCHOR_THRESHOLD_MS = 2000
local wall_clock_anchor_ms = os.time() * 1000
local uptime_anchor_seconds = uptime_seconds()

local function current_timestamp_ms()
    local wall_clock_ms = os.time() * 1000
    local current_uptime = uptime_seconds()

    if not uptime_anchor_seconds or not current_uptime
        or current_uptime < uptime_anchor_seconds then
        wall_clock_anchor_ms = wall_clock_ms
        uptime_anchor_seconds = current_uptime
        return wall_clock_ms
    end

    local uptime_derived_ms = wall_clock_anchor_ms
        + math.floor((current_uptime - uptime_anchor_seconds) * 1000)
    if math.abs(wall_clock_ms - uptime_derived_ms) > CLOCK_REANCHOR_THRESHOLD_MS then
        -- Uptime smooths sub-second timestamps, but wall time remains authoritative
        -- after NTP corrections or suspend/resume. Sequence provides ordering, so
        -- a legitimate backwards wall-clock correction must not be clamped away.
        wall_clock_anchor_ms = wall_clock_ms
        uptime_anchor_seconds = current_uptime
        return wall_clock_ms
    end

    return uptime_derived_ms
end

local function build_snapshot(next_sequence)
    local encoded_entities = {}
    for _, entry in ipairs(sorted_entities()) do
        table.insert(encoded_entities, encode_entity(entry))
    end

    local diagnostic_json = table.concat({
        "{",
        "\"dropped_unknown_events\":", json_integer(diagnostics.dropped_unknown_events),
        ",\"ambiguous_pet_events\":", json_integer(diagnostics.ambiguous_pet_events),
        ",\"missing_identity_events\":", json_integer(diagnostics.missing_identity_events),
        ",\"missing_damage_payload_events\":", json_integer(diagnostics.missing_damage_payload_events),
        ",\"unconfirmed_target_events\":", json_integer(diagnostics.unconfirmed_target_events),
        ",\"filtered_non_big_target_events\":", json_integer(diagnostics.filtered_non_big_target_events),
        ",\"target_id_native_reads\":", json_integer(diagnostics.target_id_native_reads),
        ",\"target_id_managed_reads\":", json_integer(diagnostics.target_id_managed_reads),
        ",\"invalid_damage_events\":", json_integer(diagnostics.invalid_damage_events),
        ",\"quest_state_read_failures\":", json_integer(diagnostics.quest_state_read_failures),
        ",\"training_state_read_failures\":", json_integer(diagnostics.training_state_read_failures),
        ",\"training_state_conflicts\":", json_integer(diagnostics.training_state_conflicts),
        ",\"training_state_valid\":", json_boolean(training_state_valid),
        ",\"training_detector\":", json_string(training_detector),
        ",\"mixed_pet_source_events\":", json_integer(diagnostics.mixed_pet_source_events),
        ",\"write_failures\":", json_integer(write_failures),
        ",\"last_publish_api\":", json_string(last_publish_api),
        ",\"unknown_attacker_types\":", encode_count_map(diagnostics.unknown_attacker_types),
        ",\"dropped_source_types\":", encode_count_map(diagnostics.dropped_source_types),
        ",\"unknown_attacker_targets\":", encode_count_map(diagnostics.unknown_attacker_targets),
        ",\"pet_entity_events\":", encode_count_map(diagnostics.pet_entity_events),
        ",\"pet_entity_damage\":", encode_count_map(diagnostics.pet_entity_damage),
        ",\"filtered_target_ids\":", encode_count_map(diagnostics.filtered_target_ids),
        "}",
    })

    return table.concat({
        "{",
        "\"version\":2",
        ",\"schema\":", json_string(SCHEMA_NAME),
        ",\"seq\":", json_integer(next_sequence),
        ",\"timestamp_ms\":", json_integer(current_timestamp_ms()),
        ",\"pet_aggregation\":\"per_owner\"",
        ",\"quest\":{",
        "\"active\":", json_boolean(quest_active),
        ",\"state\":", json_integer(quest_state),
        ",\"epoch\":", json_integer(quest_epoch),
        ",\"state_valid\":", json_boolean(quest_state_valid),
        ",\"collection_paused\":", json_boolean(collection_paused),
        ",\"training\":", json_boolean(quest_training),
        "}",
        ",\"entities\":[", table.concat(encoded_entities, ","), "]",
        ",\"diagnostics\":", diagnostic_json,
        "}",
    })
end

local managed_file_methods_resolved = false
local managed_file_replace = nil
local managed_file_move = nil
local publish_attempt_sequence = 0

local function resolve_managed_file_methods()
    if managed_file_methods_resolved then return end
    managed_file_methods_resolved = true

    local file_type = find_type("System.IO.File")
    if not file_type then return end
    managed_file_replace = safe_call(
        file_type,
        "get_method",
        "Replace(System.String,System.String,System.String)"
    ) or safe_call(file_type, "get_method", "Replace")
    managed_file_move = safe_call(
        file_type,
        "get_method",
        "Move(System.String,System.String)"
    ) or safe_call(file_type, "get_method", "Move")
end

local function path_exists(path)
    if not io or type(io.open) ~= "function" then return false end
    local open_ok, probe = pcall(io.open, path, "rb")
    if not open_ok then return false end
    if probe then
        pcall(probe.close, probe)
        return true
    end
    return false
end

local function cleanup_file(path)
    if os and type(os.remove) == "function" then
        pcall(os.remove, path)
    end
end

local function publish_session_id()
    local components = { tostring(os.time()), tostring({}) }
    local current_uptime = uptime_seconds()
    if current_uptime then
        table.insert(components, string.format("%.6f", current_uptime))
    end

    -- Lua's implementation asks the host for a process-safe unique name. Use
    -- that name only as entropy for our same-directory path, and remove the
    -- probe because POSIX Lua may create it while Windows Lua usually does not.
    if os and type(os.tmpname) == "function" then
        local tmpname_ok, unique_name = pcall(os.tmpname)
        if tmpname_ok and unique_name then
            table.insert(components, unique_name)
            cleanup_file(unique_name)
        end
    end

    return (table.concat(components, "-"):gsub("[^%w_%-]", "_"))
end

local PUBLISH_SESSION_ID = publish_session_id()

local function next_publish_paths()
    publish_attempt_sequence = publish_attempt_sequence + 1
    local suffix = PUBLISH_SESSION_ID .. "." .. tostring(publish_attempt_sequence)
    return OUTPUT_PATH .. ".tmp." .. suffix, OUTPUT_PATH .. ".bak." .. suffix
end

local function temp_file_was_consumed(temp_path)
    return not path_exists(temp_path)
end

local function managed_atomic_publish(temp_path)
    resolve_managed_file_methods()
    local errors = {}

    -- System.IO.File.Replace maps to the platform's replace primitive and is
    -- useful on Windows CRTs where os.rename refuses to overwrite a file.
    if managed_file_replace then
        local replace_ok, replace_error = pcall(
            managed_file_replace.call,
            managed_file_replace,
            nil,
            temp_path,
            OUTPUT_PATH,
            nil
        )
        if replace_ok and temp_file_was_consumed(temp_path) then
            return true, nil, "managed_replace"
        end
        if not replace_ok then
            replace_error = tostring(replace_error)
        else
            replace_error = "System.IO.File.Replace left the temp file in place"
        end
        table.insert(errors, replace_error)
    end

    -- Move is expected to succeed only when the formal path does not exist. It
    -- is still safe to try after Replace: it never removes or truncates it.
    if managed_file_move then
        local move_ok, move_error = pcall(
            managed_file_move.call,
            managed_file_move,
            nil,
            temp_path,
            OUTPUT_PATH
        )
        if move_ok and temp_file_was_consumed(temp_path) then
            return true, nil, "managed_move"
        end
        table.insert(errors,
            move_ok and "System.IO.File.Move left the temp file in place"
                or tostring(move_error))
    end

    if #errors == 0 then
        return false, "no managed atomic replace API", nil
    end
    return false, table.concat(errors, "; "), nil
end

local function rename_file(old_path, new_path)
    if not os or type(os.rename) ~= "function" then
        return false, "os.rename is unavailable"
    end
    local call_ok, rename_ok, rename_error = pcall(os.rename, old_path, new_path)
    if not call_ok then return false, tostring(rename_ok) end
    if rename_ok then return true, nil end
    return false, tostring(rename_error or rename_ok)
end

local function backup_swap(temp_path, backup_path)
    if not path_exists(OUTPUT_PATH) then
        return false, "formal path does not exist for backup swap", nil
    end

    local backed_up, backup_error = rename_file(OUTPUT_PATH, backup_path)
    if not backed_up then
        return false, "backup rename failed: " .. tostring(backup_error), nil
    end

    local published, publish_error = rename_file(temp_path, OUTPUT_PATH)
    if published then
        cleanup_file(backup_path)
        return true, nil, "backup_swap"
    end

    -- Do not unlink the known-good snapshot. If publishing the new file fails,
    -- restore the backup first; only the unpublished temp is expendable.
    local restored, restore_error = rename_file(backup_path, OUTPUT_PATH)
    if restored then
        return false, "new-file rename failed; old snapshot restored: "
            .. tostring(publish_error), nil
    end

    -- A failed restore is exceptional (for example, another producer won the
    -- path). Preserve the backup for recovery rather than deleting good data.
    return false, "new-file rename failed (" .. tostring(publish_error)
        .. "); backup restore failed (" .. tostring(restore_error)
        .. "); old snapshot preserved at " .. backup_path, nil
end

local function atomic_publish(payload)
    local temp_path, backup_path = next_publish_paths()
    if not io or type(io.open) ~= "function" then
        cleanup_file(temp_path)
        return false, "io.open is unavailable", nil
    end

    local open_ok, file, open_error = pcall(io.open, temp_path, "wb")
    if not open_ok then
        cleanup_file(temp_path)
        return false, file, nil
    end
    if not file then
        cleanup_file(temp_path)
        return false, open_error, nil
    end

    local write_ok, write_result, write_error = pcall(file.write, file, payload)
    if not write_ok or not write_result then
        pcall(file.close, file)
        cleanup_file(temp_path)
        return false, write_error or write_result, nil
    end

    local close_ok, close_result, close_error = pcall(file.close, file)
    if not close_ok or not close_result then
        cleanup_file(temp_path)
        return false, close_error or close_result, nil
    end

    -- Never unlink the formal file first. POSIX rename replaces an existing
    -- destination atomically, but Wine/MS CRT os.rename may reject that case.
    local renamed, rename_error = rename_file(temp_path, OUTPUT_PATH)
    if renamed then return true, nil, "rename" end

    local managed, managed_error, managed_api = managed_atomic_publish(temp_path)
    if managed then return true, nil, managed_api end

    local swapped, swap_error, swap_api = backup_swap(temp_path, backup_path)
    if swapped then return true, nil, swap_api end

    cleanup_file(temp_path)
    -- backup_swap consumes its backup on successful publish or rollback. If it
    -- still exists here, restore failed; retain the known-good copy regardless
    -- of whether another producer concurrently populated the formal path.
    return false, tostring(rename_error) .. "; " .. tostring(managed_error)
        .. "; " .. tostring(swap_error), nil
end

local function publish_snapshot(payload, next_sequence)
    -- Current REFramework builds expose fs.write and deliberately sandbox it
    -- to reframework/data. There is no rename primitive, so alternate between
    -- two complete JSON documents. The external reader parses both and picks
    -- the greatest valid sequence; a concurrently truncated slot is ignored.
    if fs and type(fs.write) == "function" then
        local slot = (next_sequence % 2 == 0) and OUTPUT_SLOT_A or OUTPUT_SLOT_B
        local write_ok, write_error = pcall(fs.write, slot, payload)
        if write_ok then
            return true, nil, "fs.write:" .. slot
        end
        return false, tostring(write_error), nil
    end

    -- Compatibility for older REFramework builds that did not expose fs.
    return atomic_publish(payload)
end

-- Resolve and install the safest available managed damage hook.
local enemy_base_type = find_type("snow.enemy.EnemyCharacterBase")
local damage_method = nil
local damage_method_name = nil
if enemy_base_type then
    for _, name in ipairs({
        -- Coavins' maintained Rise meter uses this callback and payload type:
        -- snow.hit.EnemyCalcDamageInfo.AfterCalcInfo_DamageSide.
        "afterCalcDamage_DamageSide",
        "stockDamage",
        "setDamage",
        "damage",
        "applyDamage",
        "setVitalDamage",
    }) do
        local method = safe_call(enemy_base_type, "get_method", name)
        if method then
            damage_method = method
            damage_method_name = name
            break
        end
    end
end

if damage_method then
    sdk.hook(damage_method, on_damage_pre, function(return_value)
        return return_value
    end)
    log.info("[mhr-overlay-damage] hooked: " .. damage_method_name)
else
    -- The old VitalParam.set_Current fallback only supplied a raw float. It had
    -- no independently verifiable source Id and is intentionally not counted.
    log.error("[mhr-overlay-damage] no identity-capable damage hook found")
end

re.on_pre_application_entry("UpdateBehavior", update_quest_state)

re.on_frame(function()
    write_frame_count = write_frame_count + 1
    if write_frame_count < WRITE_INTERVAL then return end
    write_frame_count = 0

    local next_sequence = sequence + 1
    local payload = build_snapshot(next_sequence)
    local published, publish_error, publish_api =
        publish_snapshot(payload, next_sequence)
    if published then
        sequence = next_sequence
        last_publish_api = publish_api or ""
        return
    end

    write_failures = write_failures + 1
    if write_failures <= 3 or write_failures % 120 == 0 then
        log.warn("[mhr-overlay-damage] snapshot publish failed; old file retained: "
            .. tostring(publish_error))
    end
end)

re.on_script_reset(function()
    -- Leave the last complete formal snapshot for the consumer's staleness
    -- check. A failed cleanup must never destroy known-good data.
    log.info("[mhr-overlay-damage] unloaded; last snapshot retained")
end)
