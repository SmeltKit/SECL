R"RULES([
  {
    "ids": [
      "better_loading_screen"
    ],
    "name": "Better Loading Screen",
    "type": "GAME",
    "status": "WONT_FIX",
    "tracking": "https://github.com/shedaniel/BetterLoadingScreen/issues/20"
  },
  {
    "ids": [
      "bigbeacons"
    ],
    "name": "Big Beacons",
    "type": "GAME",
    "status": "UNKNOWN",
    "tracking": "UNKNOWN"
  },
  {
    "ids": [
      "decorative_blocks"
    ],
    "name": "Decorative Blocks",
    "type": "GAME",
    "status": "IN_PROGRESS",
    "tracking": "https://github.com/lilypuree/Decorative-Blocks/issues/76"
  },
  {
    "ids": [
      "logbegone"
    ],
    "name": "Log Begone",
    "type": "SELF",
    "status": "BLOCKED",
    "tracking": "https://github.com/AzureDoom/Log-Begone/issues/3",
    "note": "Not all log messages are passed through the mod's filter."
  },
  {
    "ids": [
      "vulkanmod"
    ],
    "name": "VulkanMod",
    "type": "GAME",
    "status": "NO_ANSWER",
    "tracking": "https://github.com/xCollateral/VulkanMod/issues/204"
  },
  {
    "ids": [
      "codex"
    ],
    "name": "Codex",
    "type": "GAME",
    "status": "WONT_FIX",
    "tracking": "https://github.com/IMB11/Codex/issues/3"
  },
  {
    "ids": [
      "libjf"
    ],
    "name": "LibJF",
    "type": "GAME",
    "status": "ON_HOLD",
    "tracking": "https://github.com/Fabulously-Optimized/fabulously-optimized/issues/210#issuecomment-1232601249"
  },
  {
    "ids": [
      "optifabric",
      "optifine"
    ],
    "name": "OptiFabric/OptiFine",
    "type": "GAME",
    "status": "NO_ANSWER",
    "tracking": "https://github.com/Chocohead/OptiFabric/issues/751"
  },
  {
    "ids": [
      "rpgdifficulty"
    ],
    "name": "Rpg Difficulty",
    "type": "GAME",
    "status": "WONT_FIX",
    "tracking": "https://github.com/Globox1997/RpgDifficulty/issues/39"
  },
  {
    "ids": [
      "dynmap"
    ],
    "name": "Dynmap",
    "type": "GAME",
    "status": "WONT_FIX",
    "tracking": "https://github.com/webbukkit/dynmap/issues/4016"
  },
  {
    "ids": [
      "chattriggers"
    ],
    "name": "ChatTriggers",
    "type": "SELF",
    "status": "UNKNOWN",
    "tracking": "UNKNOWN",
    "note": "Modules can be loaded and managed, but are unable to be executed on Quilt."
  },
  {
    "ids": [
      "creativecore"
    ],
    "name": "CreativeCore",
    "type": "WORKAROUND",
    "status": "WONT_FIX",
    "tracking": "https://github.com/CreativeMD/CreativeCore/issues/205",
    "note": "Occasionally breaks the search tab in the creative mode item screen, and spams errors in logs. The issue can be fixed via using its `/cmdclientconfig` command to set `playSoundWithOffset` to `false`."
  },
  {
    "ids": [
      "wetlands"
    ],
    "name": "Beneath the Wetlands",
    "type": "GAME",
    "status": "NO_ANSWER",
    "tracking": "https://github.com/birchfolks/Beneath-the-Wetlands/issues/13"
  },
  {
    "ids": [
      "essential-container"
    ],
    "name": "Essential",
    "type": "WORKAROUND",
    "status": "WONT_FIX",
    "tracking": "https://gist.github.com/OroArmor/66632962acdd4bbbacc71d858befab29",
    "note": "Prevents Quilt clients from connecting to local worlds on 1.20.2-1.20.4. Add `-Dquilt.networking.reserialize_custom_payloads=true` to the java arguments. Provided direct help to developers and was turned down. Issue on their end. Will also not be present in Mod Menu."
  },
  {
    "ids": [
      "farm_and_charm"
    ],
    "name": "[Let's Do] Farm & Charm",
    "type": "SELF",
    "status": "UNKNOWN",
    "tracking": "UNKNOWN",
    "note": "Compostable items cannot be composted on Quilt"
  },
  {
    "ids": [
      "requiem"
    ],
    "name": "Requiem",
    "type": "WORKAROUND",
    "status": "WONT_FIX",
    "tracking": "https://github.com/Ladysnake/Requiem/issues/670",
    "note": "Creating an obelisk crashes the game when using some versions of QSL. Use [QSL 4.0.0-beta.30](https://modrinth.com/mod/qsl/version/4.0.0-beta.30+0.76.0-1.19.2) as a workaround."
  },
  {
    "ids": [
      "feather-loader"
    ],
    "name": "Feather Client",
    "type": "GAME",
    "status": "UNKNOWN",
    "tracking": "UNKNOWN",
    "note": "Does not finish launching Minecraft due to a missing method."
  }
])RULES"
