# Third-party sources

The native hook, overlay shim and embedded integration code derive from [NotProton](https://github.com/NotProtonNot/NotProton) v1.1.0 at commit [05a27874798ffbffb8e64a37595bde6f16d5320a](https://github.com/NotProtonNot/NotProton/commit/05a27874798ffbffb8e64a37595bde6f16d5320a). The project retains its GPLv3 license in [LICENSE](LICENSE), original source notices and [upstream licensing notice](licenses/NotProton-NOTICE).

Dobby is a separate pinned build dependency at commit `5dfc8546954ce3b3198132ab13fddb89ee92cdd7`. Its Apache 2.0 license is retained in [licenses/Dobby-LICENSE](licenses/Dobby-LICENSE). The dependency script fetches that exact revision.

The vendored cJSON source retains its MIT copyright notice. Its license is also copied to [licenses/cJSON-LICENSE](licenses/cJSON-LICENSE).

The Wine loader bridge, Valve lsteamclient and Windows Steam helper belong to the separate WineForge source project. Their original per-component licenses remain there. They are not relicensed by this repository.

The upstream NotProton notice describes components of the original application that are not included here. This project does not include its app bundle, Sparkle updater or Valve Steam client binaries.
