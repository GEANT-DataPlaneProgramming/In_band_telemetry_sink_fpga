DPDK_VER="20.08"
T4P4_HEAD="a954d12b0f9db6f27a"
JOBS=4

sudo apt install -y python python-pip clang

# Get t4p4 compiler
git clone https://github.com/P4ELTE/t4p4s.git 
cd t4p4s
git checkout $T4P4_HEAD
git submodule init
git submodule update

# Set dpdk version and prepare compilier
sed -i 's/\[ "${vsn\[0\]}/#\[ "${vsn\[0\]}'/ ./bootstrap-t4p4s.sh
MAX_MAKE_JOBS=$JOBS PARALLEL_INSTALL=0 DPDK_FILEVSN=$DPDK_VER DPDK_VSN=$DPDK_VER . ./bootstrap-t4p4s.sh
