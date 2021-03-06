/*!
 * @copyright 2018 Bonn-Rhein-Sieg University
 *
 * @author Minh Nguyen
 * @author Santosh Thoduka
 * @author Mohammad Wasil
 *
 * @brief File contains definitions for extracting a plane from a point cloud
 */
#include <mas_perception_libs/sac_plane_segmenter.h>
#include <pcl/filters/project_inliers.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <limits>

namespace mas_perception_libs
{
    PlaneModel::PlaneModel(pcl::ModelCoefficients::Ptr pPlaneCoeffsPtr)
    : mCoefficients(pPlaneCoeffsPtr->values[0], pPlaneCoeffsPtr->values[1],
                    pPlaneCoeffsPtr->values[2], pPlaneCoeffsPtr->values[3]),
      mHeader(pPlaneCoeffsPtr->header), mRangeX(std::numeric_limits<float>::max(), std::numeric_limits<float>::min()),
      mRangeY(std::numeric_limits<float>::max(), std::numeric_limits<float>::min())
    {
        mHullPointsPtr = boost::make_shared<PointCloud>();
    }

    void
    SacPlaneSegmenter::setParams(const SacPlaneSegmenterParams &pPlaneParams)
    {
        mNormalEstimation.setRadiusSearch(pPlaneParams.mNormalRadiusSearch);

        mSacSegment.setMaxIterations(pPlaneParams.mSacMaxIterations);
        mSacSegment.setDistanceThreshold(pPlaneParams.mSacDistThreshold);
        mSacSegment.setEpsAngle(pPlaneParams.mSacEpsAngle);
        mSacSegment.setOptimizeCoefficients(pPlaneParams.mSacOptimizeCoeffs);
        mSacSegment.setNormalDistanceWeight(pPlaneParams.mSacNormalDistWeight);
    }

    void
    SacPlaneSegmenter::findPlaneInliers(const PointCloud::ConstPtr &pCloudPtr, pcl::PointIndices::Ptr &pInliers,
                                        pcl::ModelCoefficients::Ptr &pCoefficients)
    {
        // estimate normals at each point, needed for the SAC segmentation
        PointCloudNormal::Ptr normalCloudPtr = boost::make_shared<PointCloudNormal>();
        mNormalEstimation.setInputCloud(pCloudPtr);
        mNormalEstimation.compute(*normalCloudPtr);

        // run the SAC segmentation algorithm
        mSacSegment.setModelType(mSacModel);
        mSacSegment.setMethodType(cSacMethodType);
        mSacSegment.setAxis(mPlaneAxis);
        mSacSegment.setInputCloud(pCloudPtr);
        mSacSegment.setInputNormals(normalCloudPtr);
        mSacSegment.segment(*pInliers, *pCoefficients);
    }

    void
    SacPlaneSegmenter::findConvexHull(
            const PointCloud::ConstPtr &pCloudPtr, const pcl::PointIndices::Ptr &pInlierIndicesPtr,
            const pcl::ModelCoefficients::Ptr &pCoefficientsPtr, PlaneModel &pPlaneModel)
    {
        // project all points onto the detected plane, a.k.a. flatten the plane
        pcl::ProjectInliers<PointT> projectInliers;
        PointCloud::Ptr plane = boost::make_shared<PointCloud>();
        projectInliers.setModelType(mSacModel);
        projectInliers.setInputCloud(pCloudPtr);
        projectInliers.setModelCoefficients(pCoefficientsPtr);
        projectInliers.setIndices(pInlierIndicesPtr);
        projectInliers.setCopyAllData(false);
        projectInliers.filter(*plane);

        // calculate the convex hull of the fitted plane
        pcl::ConvexHull<PointT> convexHull;
        convexHull.setInputCloud(plane);
        convexHull.reconstruct(*(pPlaneModel.mHullPointsPtr));

        // calculate plane height as the average of convex hull points
        if (pPlaneModel.mHullPointsPtr->points.empty())
        {
            return;
        }
        for (auto &point : pPlaneModel.mHullPointsPtr->points)
        {
            pPlaneModel.mCenter.x += point.x;

            // find range of x coordinates of hull points
            if (point.x < pPlaneModel.mRangeX[0])
                pPlaneModel.mRangeX[0] = point.x;
            if (point.x > pPlaneModel.mRangeX[1])
                pPlaneModel.mRangeX[1] = point.x;

            pPlaneModel.mCenter.y += point.y;

            // find range of y coordinates of hull points
            if (point.y < pPlaneModel.mRangeY[0])
                pPlaneModel.mRangeY[0] = point.y;
            if (point.y > pPlaneModel.mRangeY[1])
                pPlaneModel.mRangeY[1] = point.y;

            pPlaneModel.mCenter.z += point.z;
        }
        // calculate the plane's mean (x, y, z) coordinates
        pPlaneModel.mCenter.x /= pPlaneModel.mHullPointsPtr->points.size();
        pPlaneModel.mCenter.y /= pPlaneModel.mHullPointsPtr->points.size();
        pPlaneModel.mCenter.z /= pPlaneModel.mHullPointsPtr->points.size();
    }

    PlaneModel
    SacPlaneSegmenter::findPlane(const PointCloud::ConstPtr &pCloudPtr)
    {
        // run the SAC segmentation algorithm
        auto inlierIndicesPtr = boost::make_shared<pcl::PointIndices>();
        auto planeCoeffsPtr = boost::make_shared<pcl::ModelCoefficients>();
        findPlaneInliers(pCloudPtr, inlierIndicesPtr, planeCoeffsPtr);
        if (inlierIndicesPtr->indices.empty() || planeCoeffsPtr->values.empty())
        {
            throw std::runtime_error("could not fit any plane");
        }

        // calculate convex hull and plane height
        PlaneModel model(planeCoeffsPtr);
        findConvexHull(pCloudPtr, inlierIndicesPtr, planeCoeffsPtr, model);
        return model;
    }
}   // namespace mas_perception_libs
