// 台帳 BL-007: 点の並びと、ボクセル内で足す順を、ソートの実装に依存させない(B の修正)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <pcl/common/centroid.h>
#include <pcl/common/common.h>
#include <pcl/filters/voxel_grid.h>

#include <common_lib.h>

// 点を時刻(curvature)で並べる。時刻の等しい点は、元の並びを保つ
inline void sort_by_time(PointCloudXYZI &cloud)
{
    // time_list と同じ比較。std::stable_sort は const の要素も比べるので、const 参照で受ける
    std::stable_sort(cloud.points.begin(), cloud.points.end(),
                     [](const PointType &x, const PointType &y) { return x.curvature < y.curvature; });
}

// pcl::VoxelGrid(PCL 1.10、voxel_grid.hpp)の applyFilter を写し、ボクセルの添字のソートだけを
// std::stable_sort に替えたもの。同じボクセルの点は入力の順に並び、その順に足す。ほかの処理は元のまま
// 元のコード: Copyright (c) 2010-2011, Willow Garage, Inc. / 2012-, Open Perception, Inc. BSD ライセンス
template <typename PointT>
class StableVoxelGrid : public pcl::VoxelGrid<PointT>
{
protected:
    using PointCloud = typename pcl::Filter<PointT>::PointCloud;

    struct IndexIdx
    {
        unsigned int idx;
        unsigned int cloud_point_index;
        IndexIdx(unsigned int idx_, unsigned int cloud_point_index_) : idx(idx_), cloud_point_index(cloud_point_index_) {}
        bool operator<(const IndexIdx &p) const { return idx < p.idx; }
    };

    void applyFilter(PointCloud &output) override
    {
        const auto &input = this->input_;
        if (!input)
        {
            PCL_WARN("[pcl::%s::applyFilter] No input dataset given!\n", this->getClassName().c_str());
            output.width = output.height = 0;
            output.points.clear();
            return;
        }

        output.height = 1;
        output.is_dense = true;

        Eigen::Vector4f min_p, max_p;
        if (!this->filter_field_name_.empty())
            pcl::getMinMax3D<PointT>(input, *this->indices_, this->filter_field_name_,
                                     static_cast<float>(this->filter_limit_min_), static_cast<float>(this->filter_limit_max_),
                                     min_p, max_p, this->filter_limit_negative_);
        else
            pcl::getMinMax3D<PointT>(*input, *this->indices_, min_p, max_p);

        const Eigen::Array4f &inv = this->inverse_leaf_size_;
        std::int64_t dx = static_cast<std::int64_t>((max_p[0] - min_p[0]) * inv[0]) + 1;
        std::int64_t dy = static_cast<std::int64_t>((max_p[1] - min_p[1]) * inv[1]) + 1;
        std::int64_t dz = static_cast<std::int64_t>((max_p[2] - min_p[2]) * inv[2]) + 1;
        if ((dx * dy * dz) > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
        {
            PCL_WARN("[pcl::%s::applyFilter] Leaf size is too small for the input dataset. Integer indices would overflow.", this->getClassName().c_str());
            output = *input;
            return;
        }

        Eigen::Vector4i &min_b = this->min_b_, &max_b = this->max_b_, &div_b = this->div_b_, &divb_mul = this->divb_mul_;
        min_b[0] = static_cast<int>(std::floor(min_p[0] * inv[0]));
        max_b[0] = static_cast<int>(std::floor(max_p[0] * inv[0]));
        min_b[1] = static_cast<int>(std::floor(min_p[1] * inv[1]));
        max_b[1] = static_cast<int>(std::floor(max_p[1] * inv[1]));
        min_b[2] = static_cast<int>(std::floor(min_p[2] * inv[2]));
        max_b[2] = static_cast<int>(std::floor(max_p[2] * inv[2]));
        div_b = max_b - min_b + Eigen::Vector4i::Ones();
        div_b[3] = 0;
        divb_mul = Eigen::Vector4i(1, div_b[0], div_b[0] * div_b[1], 0);

        std::vector<IndexIdx> index_vector;
        index_vector.reserve(this->indices_->size());

        std::vector<pcl::PCLPointField> fields;
        int distance_idx = -1;
        if (!this->filter_field_name_.empty())
        {
            distance_idx = pcl::getFieldIndex<PointT>(this->filter_field_name_, fields);
            if (distance_idx == -1)
                PCL_WARN("[pcl::%s::applyFilter] Invalid filter field name. Index is %d.\n", this->getClassName().c_str(), distance_idx);
        }
        for (std::vector<int>::const_iterator it = this->indices_->begin(); it != this->indices_->end(); ++it)
        {
            const PointT &p = input->points[*it];
            if (!input->is_dense)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    continue;

            if (!this->filter_field_name_.empty())
            {
                const std::uint8_t *pt_data = reinterpret_cast<const std::uint8_t *>(&p);
                float distance_value = 0;
                memcpy(&distance_value, pt_data + fields[distance_idx].offset, sizeof(float));
                if (this->filter_limit_negative_)
                {
                    if ((distance_value < this->filter_limit_max_) && (distance_value > this->filter_limit_min_))
                        continue;
                }
                else
                {
                    if ((distance_value > this->filter_limit_max_) || (distance_value < this->filter_limit_min_))
                        continue;
                }
            }

            int ijk0 = static_cast<int>(std::floor(p.x * inv[0]) - static_cast<float>(min_b[0]));
            int ijk1 = static_cast<int>(std::floor(p.y * inv[1]) - static_cast<float>(min_b[1]));
            int ijk2 = static_cast<int>(std::floor(p.z * inv[2]) - static_cast<float>(min_b[2]));
            int idx = ijk0 * divb_mul[0] + ijk1 * divb_mul[1] + ijk2 * divb_mul[2];
            index_vector.emplace_back(static_cast<unsigned int>(idx), *it);
        }

        // BL-007: 元は std::sort。同じボクセルの点を入力の順に保つ
        std::stable_sort(index_vector.begin(), index_vector.end());

        unsigned int total = 0;
        unsigned int index = 0;
        std::vector<std::pair<unsigned int, unsigned int>> first_and_last_indices_vector;
        first_and_last_indices_vector.reserve(index_vector.size());
        while (index < index_vector.size())
        {
            unsigned int i = index + 1;
            while (i < index_vector.size() && index_vector[i].idx == index_vector[index].idx)
                ++i;
            if (i - index >= this->min_points_per_voxel_)
            {
                ++total;
                first_and_last_indices_vector.emplace_back(index, i);
            }
            index = i;
        }

        output.points.resize(total);
        if (this->save_leaf_layout_)
        {
            try
            {
                std::uint32_t new_layout_size = div_b[0] * div_b[1] * div_b[2];
                std::uint32_t reinit_size = std::min(static_cast<unsigned int>(new_layout_size), static_cast<unsigned int>(this->leaf_layout_.size()));
                for (std::uint32_t i = 0; i < reinit_size; i++)
                    this->leaf_layout_[i] = -1;
                this->leaf_layout_.resize(new_layout_size, -1);
            }
            catch (std::bad_alloc &)
            {
                throw pcl::PCLException("VoxelGrid bin size is too low; impossible to allocate memory for layout", "voxel_grid.hpp", "applyFilter");
            }
            catch (std::length_error &)
            {
                throw pcl::PCLException("VoxelGrid bin size is too low; impossible to allocate memory for layout", "voxel_grid.hpp", "applyFilter");
            }
        }

        index = 0;
        for (const auto &cp : first_and_last_indices_vector)
        {
            unsigned int first_index = cp.first;
            unsigned int last_index = cp.second;
            if (this->save_leaf_layout_)
                this->leaf_layout_[index_vector[first_index].idx] = index;

            if (!this->downsample_all_data_)
            {
                Eigen::Vector4f centroid(Eigen::Vector4f::Zero());
                for (unsigned int li = first_index; li < last_index; ++li)
                    centroid += input->points[index_vector[li].cloud_point_index].getVector4fMap();
                centroid /= static_cast<float>(last_index - first_index);
                output.points[index].getVector4fMap() = centroid;
            }
            else
            {
                pcl::CentroidPoint<PointT> centroid;
                for (unsigned int li = first_index; li < last_index; ++li)
                    centroid.add(input->points[index_vector[li].cloud_point_index]);
                centroid.get(output.points[index]);
            }
            ++index;
        }
        output.width = static_cast<std::uint32_t>(output.points.size());
    }
};
