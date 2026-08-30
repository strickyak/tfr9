# tfr9/v4/misc/fetch-pico-sdk-and-tool.sh
#
# Because of `cd "$(dirname "$0")"`
# always keep this script inside the tfr9/v4/misc/ directory
# so that ../../.. will be the top-level shelf on which tfr9 sits.
set -ex

VERSION=2.3.0
cd "$(dirname "$0")"

TOP="$(cd ../../.. && pwd)"
export PATH="$TOP/bin:$PATH" 

# Use a temporary directory under /tmp/
T="/tmp/pico.$$.dir"
mkdir $T
cd $T

# Fetch entire pico-sdk with submodules.
git clone -b $VERSION https://github.com/raspberrypi/pico-sdk.git
( cd pico-sdk && git submodule update --init )
rm -rf "$TOP/pico-sdk"
cp -a pico-sdk "$TOP/pico-sdk"

# Fetch picotool
git clone -b $VERSION https://github.com/raspberrypi/picotool.git
rm -rf "$TOP/picotool"
cp -a pico-sdk "$TOP/picotool"

# Now build and install the picotool to the TOP shelf.
mkdir build
cd build
PICOTOOL_DIR="$TOP" PICO_SDK_PATH="$TOP/pico-sdk/" cmake -DCMAKE_INSTALL_PREFIX="$TOP" ../picotool
make
make install
