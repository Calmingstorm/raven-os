# SeaBIOS: corresponding source

`../seabios.bin` and `../vgabios.bin` are SeaBIOS 1.16.2 and its VGA BIOS. The v86 project built them in
[copy/v86 commit b8a39b11](https://github.com/copy/v86/commit/b8a39b11dd2076870699e6cac053556271b9bfab)
("Update SeaBIOS to 1.16.2 (#890)") using the script and configuration in this directory. SeaBIOS is licensed under the
LGPL-3.0; see `COPYING.LESSER`.

| File | What it is |
|---|---|
| `seabios-1.16.2.tar.gz` | The release tarball from https://www.seabios.org/downloads/. sha256 `efa8d529442ad168056f94ac6666b1b3543c62ff7af6d9e020561f4d0e32f959`. Identical to git tag `rel-1.16.2` (`ea1b7a0733906b8425d948ae94fba63c32b1d425`) of https://git.seabios.org/seabios.git, apart from the release's `.version` file. |
| `seabios.config` | The build configuration v86 used for `seabios.bin` and `vgabios.bin`. |
| `fetch-and-build-seabios.sh` | v86's build script. It also builds debug variants from `seabios-debug.config`, which RavenOS doesn't ship. |
| `COPYING.LESSER` | The LGPL-3.0 text as shipped in v86's `bios/` directory. |

To rebuild:

```bash
tar -xzf seabios-1.16.2.tar.gz
cp seabios.config seabios-1.16.2/.config
make -C seabios-1.16.2
# seabios-1.16.2/out/bios.bin is seabios.bin; seabios-1.16.2/out/vgabios.bin is vgabios.bin
```

sha256 of the shipped binaries:

```text
73e3f359102e3a9982c35fce98eb7cd08f18303ac7f1ba6ebfbe6cdc1c244d98  seabios.bin
a4bc0d80cc3ca028c73dafa8fee396b8d054ce87ebd8abfbd31b06b437607880  vgabios.bin
```
