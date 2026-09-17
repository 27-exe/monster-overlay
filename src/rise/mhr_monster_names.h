// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace mhw {

// Official localized name for a Rise monster, keyed by the HunterPie schema
// Id that the game keeps at monster + 0x2D4 (MHRGame.cs:305-324 reads the same
// field, and MHRMonster.cs:46-48 turns it into a name).
//
// Source (provenance — full transcript in the repo's v0.8.4-r18 evidence dir
// `.../v0.8.4-r18/monster-identity/REPORT.md`):
//   HunterPie official localization `zh-cn.xml`
//     https://cdn.hunterpie.com/localization/zh-cn.xml
//     sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
//   byte-identical to HunterPie/localization@main/localization/zh-cn.xml and to
//   the hash published by https://api.hunterpie.com/v1/localization/checksum.
//   Extracted 2026-09-17 from `zh-cn.xml` lines 981-1140 — the <Rise> child of
//   the top-level <Monsters> block, i.e. /Strings/Monsters/Rise/Monster[@Id] —
//   79 entries, Id 0..46 / 76..98 / 107..115.
//
// The lookup mirrors HunterPie exactly:
//   MHRMonster.cs:48
//     Name = localizationRepository.FindStringBy(
//         $"//Strings/Monsters/Rise/Monster[@Id='{Id}']")
// so these are the strings a mature Rise overlay shows in-game.
//
// Returns nullptr when the localization file has no entry for `id` (the id
// gaps 47..75 / 99..106 and anything outside 0..115 are upstream and real);
// callers fall back to "Monster #<id>". Never returns an empty string.
const char *riseMonsterName(int id);

} // namespace mhw
