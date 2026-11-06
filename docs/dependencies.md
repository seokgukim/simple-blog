# C dependencies and licenses

The builder compiles the following third-party C components into its executable.  
The project downloads them into ignored `vendor/`; they are not committed to this repository.  
The project's own license is [MIT](../LICENSE), but that does not replace the terms applicable to the third-party components.

| Component | Version | Provenance                                               | License                     |
| --------- | ------- | -------------------------------------------------------- | --------------------------- |
| MD4C      | 0.5.2   | fetched by `scripts/fetch-md4c.sh`                       | MIT                         |
| libyaml   | 0.2.5   | release archive fetched by `scripts/fetch-yaml.sh`       | MIT-style permission notice |
| itex2MML  | 1.6.1   | official distribution fetched by `scripts/fetch-itex.sh` | GPL + MPL + LGPL            |

For itex2MML, the build copies `y.tab.c`, `lex.yy.c`, `y.tab.h` and `itex2MML.h` from `itex-src/perl/` and applies `scripts/patches/itex-safety.patch` to a build copy.  
The generated `y.tab.c` includes a GNU Bison skeleton notice with a special exception; read that notice in the source rather than assuming it applies to all upstream code.  
The distribution also has `itex-src/perl/LICENSE` containing the Artistic License, but the README does not establish that this file is the license for every compiled component.  
Check the relevant upstream source notices and license terms before distributing the compiled executable; this summary is not a legal determination of redistribution obligations.  
Preserve applicable third-party copyright and license notices when redistributing.

## Integrity and cache behavior

The fetch scripts contain SHA-256 pins for MD4C files and for the libyaml and itex2MML release archives.  
New downloads are checked against those pins before use.  
On subsequent runs, MD4C files are checked again, but libyaml and itex2MML are reused when their `.ready` marker exists; their extracted files are not rehashed on each build.  
The MD4C and itex2MML patches are applied to copies in `build/`, not to the downloaded vendor sources.  
A checksum match establishes agreement with the configured pin, not upstream authorship or freedom from vulnerabilities.

To independently check cached archive-based dependencies, fetch the corresponding archives into a separate temporary directory, verify their hashes against the scripts, extract them, and compare their source trees with the cached copies.  
Do not overwrite a cache that may contain local changes without inspecting it first.

Wrangler is separate deployment tooling, not a C dependency; its direct version is pinned, but its transitive npm dependencies are not lockfile-pinned.
