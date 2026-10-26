// pointlio_mapping の ROS ノード。改造前の laserMapping.cpp の main() から、ROS に依存する部分(初期化・購読・発行・ループ)を移した。
#include <cstdlib>
#include <fstream>
#include <tf/transform_broadcaster.h>
#include "li_initialization.h"
#include "mapping_core.h"

class RosOutput : public MappingOutput
{
public:
    explicit RosOutput(ros::NodeHandle &nh)
    {
        pubLaserCloudFullRes = nh.advertise<sensor_msgs::PointCloud2>
                ("/cloud_registered", 1000);
        pubLaserCloudFullRes_body = nh.advertise<sensor_msgs::PointCloud2>
                ("/cloud_registered_body", 1000);
        pubLaserCloudMap = nh.advertise<sensor_msgs::PointCloud2>
                ("/Laser_map", 1000);
        pubOdomAftMapped = nh.advertise<nav_msgs::Odometry>
                ("/aft_mapped_to_init", 1000);
        pubPath          = nh.advertise<nav_msgs::Path>
                ("/path", 1000);
    }

    void odometry(const nav_msgs::Odometry &odomAftMapped) override
    {
        pubOdomAftMapped.publish(odomAftMapped);

        static tf::TransformBroadcaster br;
        tf::Transform                   transform;
        tf::Quaternion                  q;
        transform.setOrigin(tf::Vector3(odomAftMapped.pose.pose.position.x, \
                                        odomAftMapped.pose.pose.position.y, \
                                        odomAftMapped.pose.pose.position.z));
        q.setW(odomAftMapped.pose.pose.orientation.w);
        q.setX(odomAftMapped.pose.pose.orientation.x);
        q.setY(odomAftMapped.pose.pose.orientation.y);
        q.setZ(odomAftMapped.pose.pose.orientation.z);
        transform.setRotation( q );
        br.sendTransform( tf::StampedTransform( transform, odomAftMapped.header.stamp, "camera_init", "body") );
    }
    void path(const nav_msgs::Path &msg) override { pubPath.publish(msg); }
    void cloud_registered(const sensor_msgs::PointCloud2 &msg) override { pubLaserCloudFullRes.publish(msg); }
    void cloud_registered_body(const sensor_msgs::PointCloud2 &msg) override { pubLaserCloudFullRes_body.publish(msg); }
    void laser_map(const sensor_msgs::PointCloud2 &msg) override { pubLaserCloudMap.publish(msg); }

private:
    ros::Publisher pubLaserCloudFullRes, pubLaserCloudFullRes_body, pubLaserCloudMap, pubOdomAftMapped, pubPath;
};

int main(int argc, char** argv)
{
    ros::init(argc, argv, "laserMapping");
    ros::NodeHandle nh("~");
    ros::AsyncSpinner spinner(0);
    spinner.start();
    readParameters(nh);
    // 設定の同値の検査(ハーネスのプラン T11)用。環境変数があるときだけ、読み込んだ値を書き出す
    if (const char *dump = std::getenv("POINTLIO_DUMP_PARAMS"))
    {
        std::ofstream f(dump);
        dump_parameters(f);
    }
    cout<<"lidar_type: "<<lidar_type<<endl;
    mapping_init();
    open_debug_logs();

    /*** ROS subscribe initialization ***/
    ros::Subscriber sub_pcl = p_pre->lidar_type == AVIA ? \
        nh.subscribe(lid_topic, 200000, livox_pcl_cbk) : \
        nh.subscribe(lid_topic, 200000, standard_pcl_cbk);
    ros::Subscriber sub_imu = nh.subscribe(imu_topic, 200000, imu_cbk);

    RosOutput out(nh);
//------------------------------------------------------------------------------------------------------
    signal(SIGINT, SigHandle);
    ros::Rate loop_rate(500);
    bool status = ros::ok();
    while (status)
    {
        if (flg_exit) break;
        ros::spinOnce();
        if(sync_packages(Measures))
        {
            process_measures(out);
        }
        status = ros::ok();
        loop_rate.sleep();
    }
    mapping_finish();
    return 0;
}
