

#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
 
    std::vector<cv::DMatch>  kptMatches_roi;

    for (auto match : kptMatches){
        cv::KeyPoint kp = kptsCurr.at(match.trainIdx);
        if (boundingBox.roi.contains(kp.pt)){
            kptMatches_roi.push_back(match);
        }
    }

    double d_ave = 0;
    for (auto match_roi : kptMatches_roi){
        d_ave += match_roi.distance; 
    }
    if (kptMatches_roi.size() > 0)
         d_ave = d_ave/kptMatches_roi.size();
    else return;

    double th = d_ave * 0.8;
    
    for (auto match_roi : kptMatches_roi){
        if(match_roi.distance < th){
            boundingBox.kptMatches.push_back(match_roi);
        }
    }

    std::cout << "number of keypoint matches: " << boundingBox.kptMatches.size()   << std::endl;



}

// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
    
    vector<double> h1h0s; 

    // see the distances of all kay points combinations 
    for (auto match1 = kptMatches.begin(); match1 != kptMatches.end()- 1; ++match1  ){ 
       
        cv::KeyPoint point1_curr = kptsCurr.at(match1->trainIdx);
        cv::KeyPoint point1_prev = kptsPrev.at(match1->queryIdx);

        for (auto match2 = kptMatches.begin() + 1; match2 != kptMatches.end() ; ++match2 ){

            cv::KeyPoint point2_curr = kptsCurr.at(match2->trainIdx);
            cv::KeyPoint point2_prev = kptsPrev.at(match2->queryIdx);

            double h1 = cv::norm(point1_curr.pt - point2_curr.pt);
            double h0 = cv::norm(point1_prev.pt - point2_prev.pt);

            double h_min = 100;  // threshold to make the result stable

            if(h0 > 0 && h1 > h_min && h0 > h_min){ 
                h1h0s.push_back(h1 / h0);   
            }

        }

    }

    // sort the value so that it ipossible to get the medium value to remove the impact of outliers 
    std::sort(h1h0s.begin(), h1h0s.end()); 
    std::cout << "number of h1h0s: " << h1h0s.size() <<  " h1h0s.size()/2: " << h1h0s.size()/2 <<  std::endl;

    double h1h0 = h1h0s[h1h0s.size()/2];  
    double dt = 1 / frameRate; 

    TTC = - dt / (1 - h1h0); 

    std::cout << "Camera Results>  dt:" << dt << "  h1h0: " <<  h1h0 << "  frameRate: " << frameRate << "  TTC:" << TTC << std::endl;


}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    
    // Sort points by distance (x) so that it is possible to get median value to remove the impact of outliers 
    std::sort(lidarPointsPrev.begin(), lidarPointsPrev.end(), [](LidarPoint a, LidarPoint b){
        return a.x < b.x;
    });
    std::sort(lidarPointsCurr.begin(), lidarPointsCurr.end(), [](LidarPoint a, LidarPoint b){
        return a.x < b.x;
    });

    // take median value to remove the impact of outliers  
    double d0 = lidarPointsPrev[lidarPointsPrev.size()/2].x; 
    double d1 = lidarPointsCurr[lidarPointsCurr.size()/2].x; 
    
    double dt = 1.0 / frameRate; 

    TTC = d1  * dt / (d0 - d1);

    std::cout << "Lidar Results>  d0:" << d0 << "  d1: " <<  d1 << "  frameRate: " << frameRate << "  TTC:" << TTC << std::endl;

}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    // ...
    int prev_counts = prevFrame.boundingBoxes.size();
    int curr_counts = currFrame.boundingBoxes.size(); 

    //std::cout << "previous bounding boxes: " << prev_counts  << " current boxes: " << curr_counts << " matches length: " << matches.size() <<  std::endl; 

    int pt_counts[prev_counts][curr_counts] = {0};

    for (auto match : matches){
        
        cv::KeyPoint kp_prev = prevFrame.keypoints[match.queryIdx];
        cv::KeyPoint kp_curr = currFrame.keypoints[match.trainIdx];
    
        std::vector<int> ids_prev, ids_curr; 

        for (auto bbox : prevFrame.boundingBoxes){
            if (bbox.roi.contains(kp_prev.pt)){
                ids_prev.push_back(bbox.boxID);
            }
        }
        for (auto bbox : currFrame.boundingBoxes){
            if (bbox.roi.contains(kp_curr.pt)){
                ids_curr.push_back(bbox.boxID);
            }
        }

        //std::cout << "size of id_prev: " << ids_prev.size() << " size of id_curr: " << ids_curr.size() << std::endl; 

        if (ids_prev.size() > 0 && ids_curr.size() > 0){
            for (int id_prev : ids_prev){
                for (int id_curr : ids_curr){
                    //std::cout << "id_curr: " << id_curr  << std::endl;
                    pt_counts[id_prev][id_curr] += 1; 
                }
            }
        }

    }

    /*
    std::cout << "checking the matrix" << std::endl; 
    for (int i = 0; i < sizeof(pt_counts)/sizeof(*pt_counts);  i++ ){
        for (int j = 0; j < sizeof(pt_counts[i])/sizeof(int);  j++ ){
             std::cout << pt_counts[i][j] << " "; 
        }
        std::cout << endl; 
    }
    */



    for (int i = 0; i < prev_counts; i++){

        int count_max = 0;
        int id_max =0;

        for (int j = 0; j < curr_counts ; j++){
            if (pt_counts[i][j] > count_max){
                 count_max = pt_counts[i][j];
                 id_max = j;
            }

        }
        // std::cout << "id_max: " << id_max << std::endl; 

        bbBestMatches[i] = id_max;
    }

    std::cout << "bbBestMatches.size(): " << bbBestMatches.size() << std::endl; 

}

