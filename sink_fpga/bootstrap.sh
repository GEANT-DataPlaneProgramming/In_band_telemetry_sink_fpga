## NFB Drivers ##
git clone -b master git@gitlab.liberouter.org:ndk/swbase.git
cd swbase
sed -i "s/bin\/sh/bin\/bash/" build.sh
sudo ./build.sh --bootstrap
sudo apt remove -y libfdt-dev libfdt1
wget http://cz.archive.ubuntu.com/ubuntu/pool/main/d/device-tree-compiler/libfdt1_1.4.5-3_amd64.deb
wget http://cz.archive.ubuntu.com/ubuntu/pool/main/d/device-tree-compiler/libfdt-dev_1.4.5-3_amd64.deb
sudo apt install ./libfdt1_1.4.5-3_amd64.deb ./libfdt-dev_1.4.5-3_amd64.deb
./build.sh --deb
sudo apt install -y ./cmake-build/netcope-common-*-dirty.deb 
cd ..
echo "options nfb boot_linkdown_enable=N" | sudo tee /etc/modprobe.d/liberouter.conf
sudo rmmod nfb; sudo modprobe nfb;

## P4DEV ##
git clone -b p4int-demo --recursive git@gitlab.liberouter.org:p4/p4base.git
sudo apt install -y libtool
cd p4base/sw/libp4dev
bash ./bootstrap.sh
sed -i "s/device=generic/device=nfb/" create-deb-package.sh
./create-deb-package.sh
sudo apt install ./libp4dev_*.deb
cd ../../..

## INFLUXDB ##
git submodule init
git submodule update
mkdir ../influxdb-cxx/build
cd ../influxdb-cxx/build
sudo apt-get install -y libboost-all-dev
sudo apt-get install -y libcurl4-openssl-dev
cmake ..
sudo make -j4 install
