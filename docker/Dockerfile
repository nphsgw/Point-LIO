FROM ros:noetic-ros-base

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential git libeigen3-dev libpcl-dev libomp-dev libgoogle-glog-dev libgflags-dev \
      ros-noetic-pcl-conversions ros-noetic-pcl-ros ros-noetic-tf \
      ros-noetic-eigen-conversions ros-noetic-cv-bridge ros-noetic-rviz \
      python3-catkin-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /ws/src
RUN git clone https://github.com/Livox-SDK/livox_ros_driver.git
WORKDIR /ws
RUN . /opt/ros/noetic/setup.sh && catkin_make -DCATKIN_WHITELIST_PACKAGES=livox_ros_driver
