# Operator and bot type labels

The reader now publishes `operatorId`, `heroId`, `npcTemplate`, and
`identityVerified`. The Android feed resolves those fields into `actorType`,
displayed beside the name and distance. Player and teammate panels each have an
independent **Operator / character** switch; the bot panel has **Bot role / type**.
Type labels also work with Names and Distance disabled. Existing drawing
preferences migrate to v5 with their old bits preserved and type labels enabled.

## Runtime sources

- Require the matching native ADFMCharacter ancestry (registration index 0xf31).
- For AI characters, require ADFMAICharacter ancestry (0xfac) and read the
  replicated FSpawnBaseAIInfo.npcName FString at actor+0x3d30. The enclosing
  NpcTemplateTable pointer is RepSkip and was null in live samples.
- For operators, validate DFMCharacterBattleClassComponent (0x1a57) at
  actor+0x38d0, its UObject Outer at +0x10, and its character backlink at +0x118.
  Read the active ExpertId at component+0x100.
- PlayerState.ExpertId at +0x9f8 was zero in live players while the component
  held active IDs 10010 and 40010. Cosmetic HeroId at PlayerState+0x9f0 varied
  independently. The pawn's DefaultHeroID was shared by different operators.
  Neither cosmetic ID nor the default model selects an operator label.
- The replicated PlayerState RoleType byte was zero on all sampled players and
  is deliberately not used to call them bosses.

The verified friendly-name catalog currently contains **Hackclaw (20003)**,
confirmed by selecting Hackclaw in the firing-range loadout and observing the
active component ID. Eight earlier ID/name assignments were assumptions and
have been removed. Other valid IDs display `Operator <id>` until their names
are verified. An unavailable identity displays `Unknown operator/bot`.
Bot template labels retain role/variant tokens, including Boss and Guard, and
normalize Basic/Normal to Regular. They never infer bosses or guards from HP.
Unknown valid templates retain their descriptive tokens rather than borrowing
another actor's role. This does not claim friendly names for every future
operator, boss, or skin.

## Verification

- Android debug build and all 51 JVM tests passed. Identity tests cover operator
  IDs and unknown IDs, regular/elite/specialist/animal templates, Boss/Guard
  distinctions, malformed templates, operator bots, and independent label flags.
- Expanded native reader regression passed on device. It covers active operator
  changes, zero PlayerState ExpertId, component ownership/class/backlink
  rejection, template priority, and invalid template characters.
- A live installed snapshot contained 13 combat actors with 13 verified
  identities: an operator ID, Crocodile, Regular AR/SMG/Shotgun, Elite Shotgun, and
  Shield 01. A screenshot confirms type labels beside bot names/distances.
  No live boss/guard was observed; those strings were tested in fixtures.
- Device UI tests toggled each category's type switch off/on and verified the
  saved masks (players/teammates 127→63→127, bots 125→61→125). Other drawing
  settings were preserved. All type switches were restored on.
- Game PID 27845 remained running. No hooks or game-memory writes were added;
  kptr_restrict remained 2.

Installed reader/APK asset/source binary SHA256:
`4eb927bf338df1d1b56d2c8b9464cd8cdb7b23b438729e1fff9b64de65bf8896`.

An exploratory read at the SDK's historical GObjects address returned no usable
array, so global object enumeration was not used. Identity discovery only
follows the bounded actor/component fields above.
