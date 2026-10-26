# ベースはダイジェスト、直接入れるパッケージは版、livox_ros_driver はコミットで固定する。
# 依存として入る残りのパッケージは固定していない。ビルド後に docker/check_packages.sh で packages.lock と照合する。
# 固定した版が配布元から消えると apt-get が失敗する(黙って別の版にはならない)。
FROM ros:noetic-ros-base@sha256:72b8bc59035dc0a5b8e07aae28c16caa84192971d72d207c72ed734fb1d5e97d

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential=12.8ubuntu1.1 \
      git=1:2.25.1-1ubuntu3.14 \
      libeigen3-dev=3.3.7-2 \
      libpcl-dev=1.10.0+dfsg-5ubuntu1 \
      libomp-dev=1:10.0-50~exp1 \
      libgoogle-glog-dev=0.4.0-1build1 \
      libgflags-dev=2.2.2-1build1 \
      ros-noetic-pcl-conversions=1.7.4-1focal.20250520.005606 \
      ros-noetic-pcl-ros=1.7.4-1focal.20250521.004323 \
      ros-noetic-tf=1.13.4-1focal.20250520.001125 \
      ros-noetic-eigen-conversions=1.13.4-1focal.20250426.012425 \
      ros-noetic-cv-bridge=1.16.2-1focal.20250519.235655 \
      ros-noetic-rviz=1.14.26-1focal.20250520.002916 \
      python3-catkin-tools=0.9.4-1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /ws/src
RUN git clone https://github.com/Livox-SDK/livox_ros_driver.git \
    && git -C livox_ros_driver checkout --detach 3d240d5666129e1a3052e78ee8487a04b08fdda3
WORKDIR /ws
RUN . /opt/ros/noetic/setup.sh && catkin_make -DCATKIN_WHITELIST_PACKAGES=livox_ros_driver
