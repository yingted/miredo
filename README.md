# Miredo for Android
## Building
1. Install the toolchain for some API Level: `$NDK/build/tools/make-standalone-toolchain.sh --platform=android-17 --install-dir=$HOME/bin/arm-linux-androideabi-17`
2. Build `libJudy` and point `JUDY_SRC` to your `judy-xxx/src`.
3. Build miredo.zip: `./android.sh`

`miredo.zip` unpacks to `./`, not `miredo/`
## Installing
If you have `unzip`, you can use the portion of `android.sh` after `exit`.
Otherwise, take a look at MireDroid, which comes with an installer.
