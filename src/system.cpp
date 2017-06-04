#include "system.h"

using namespace std;
using namespace pcl;
using namespace cv;

void SLAMsystem(string commonPath, string yamlPath){
    srand(0);

    STEREO_RECTIFY_PARAMS srp; // used to store
	//================obtain file name================================
	string leftImgFilePath, rightImgFilePath;
    leftImgFilePath = commonPath + "/image_0";
    rightImgFilePath = commonPath + "/image_1";

    // leftImgFilePath = commonPath + "/image_2";
    // rightImgFilePath = commonPath + "/image_3";

    vector<string> leftImgName = getImgFileName(leftImgFilePath);
    vector<string> rightImgName = getImgFileName(rightImgFilePath);

    //================read settings===================================
    cv::FileStorage fsSettings(yamlPath, cv::FileStorage::READ);
    if(!fsSettings.isOpened()){
        cerr << "ERROR: Wrong path to settings" << endl;
        return;
    }
    string savePathBefore = fsSettings["trajectory file before"];
    string savePath = fsSettings["trajectory file after"];
    string g2ofile = fsSettings["g2ofile"];
    fsSettings["LEFT.P"] >> srp.P1;
    fsSettings["RIGHT.P"] >> srp.P2;
    cout << g2ofile << endl;

    srp.imageSize.height = fsSettings["height"];
    srp.imageSize.width = fsSettings["width"];
    //================threshold======================================
    int localTimes = fsSettings["local optimization times"];
    int dataLength = fsSettings["data length"];
    int startIdx = fsSettings["start index"];

    //================set up data length ============================
    if(dataLength == 0){
        dataLength = (int)leftImgName.size();
    }
    else{
        vector<string>::const_iterator first = leftImgName.begin()+ startIdx;
        vector<string>::const_iterator last = leftImgName.begin()+ startIdx + dataLength;
        vector<string> tempLeft(first, last);
        leftImgName = tempLeft;

        first = rightImgName.begin()+ startIdx;
        last = rightImgName.begin() + startIdx+ dataLength;
        vector<string> tempRight(first, last);
        rightImgName = tempRight;
    }
    if(srp.P1.empty() || srp.P2.empty()){
        cerr << "ERROR: Calibration parameters to rectify stereo are missing!" << endl;
        return;
    }


    //============ test g2o =======================================
    
        // PoseOpt testPose;
        // testPose.optimizer.load(g2ofile.c_str());


        // int count = 0;

        // for(g2o::HyperGraph::EdgeSet::const_iterator it = testPose.optimizer.edges().begin();
        //                                              it!= testPose.optimizer.edges().end();
        //                                              it++) {
        //     g2o::EdgeSE3* edge = dynamic_cast<g2o::EdgeSE3*>(*it);
        //     cout << edge->information() << endl;
        // }
      


        // testPose.solve();
    

    //=========== test ends =========================================

    //=============== initialize system ============================================
    vector<Mat> relativeRvec_cam, relativeTvec_cam;
    Map map;
    Mapviewer mapviewer;
    vector<Frame* > allFrame;

    int deleteFrame = -5;

    Problem globalBAProblem;
    Optimizer ba(false, globalBAProblem);

    pcl::visualization::CloudViewer viewer("Cloud Viewer");
    PointCloud<PointXYZRGB>::Ptr cloud (new PointCloud<PointXYZRGB>);

    Mat accumRvec = Mat::zeros(3,1,CV_64F);
    Mat accumTvec = Mat::zeros(3,1,CV_64F);

    Frame* prevFrame = new Frame(leftImgName[0], rightImgName[0], srp, 0, &map);
    prevFrame->scenePtsinWorld = prevFrame->scenePts;

    allFrame.push_back(prevFrame);

    Frame* refFrame = allFrame.back();

    for(int n = 1; n < dataLength; n++){
        cout << endl<<endl<<"current frame number: " << n << endl;
        Frame* currFrame = new Frame(leftImgName[n], rightImgName[n], srp, n, &map);
        prevFrame->matchFrame(currFrame);
        //=================== accumlate motion ====================
        relativeRvec_cam.push_back(currFrame->rvec);
        relativeTvec_cam.push_back(currFrame->tvec);

        getAccumulateMotion(relativeRvec_cam, relativeTvec_cam,
                            0, n, accumRvec, accumTvec);

        currFrame->setWrdTransVectorAndTransScenePts(accumRvec, accumTvec);
        prevFrame->updateDescriptor(currFrame);
        prevFrame->manageMapPoints(currFrame);
      
        allFrame.push_back(currFrame);
        //=================== perform more matching ====================
        double prevMove = 0;
        if(n > 0){
            for(int t = localTimes-1; t > 0; t--){
                refFrame->matchFrame(allFrame[MAX(0, n-t)], true);
                refFrame->updateDescriptor(allFrame[MAX(0, n-t)]);
                refFrame->manageMapPoints(allFrame[MAX(0, n-t)]);

                // double currMove = normOfTransform(allFrame[MAX(0, n-t)]->rvec, allFrame[MAX(0, n-t)]->tvec);
                // if(currMove > prevMove && currMove < (localTimes-t)*3.0){
                //     prevMove = currMove;
                //     refFrame->addEdgeConstrain(allFrame[MAX(0, n-t)]->frameID, 
                //                                allFrame[MAX(0, n-t)]->rvec, 
                //                                allFrame[MAX(0, n-t)]->tvec);
                //     cout << "add edge from: "<<refFrame->frameID<<" to: " << allFrame[MAX(0, n-t)]->frameID<<"  " << allFrame[MAX(0, n-t)]->tvec.at<double>(2,0) << endl;
                // }

            }
            ba.localBundleAdjustment(allFrame, MAX(0, (int)allFrame.size()-localTimes), localTimes);
            // refFrame->judgeBadPoints();
            for(int t = localTimes-1; t>0; t--){
                allFrame[MAX(0, n-t)]->judgeBadPoints();
            }

            //============== update accumulative transformation ================
            for(int n = MAX(0, (int)allFrame.size()-localTimes)+1; n < allFrame.size(); n++) {

                getRelativeMotion(allFrame[n-1]->worldRvec, allFrame[n-1]->worldTvec,
                                  allFrame[n]->worldRvec,   allFrame[n]->worldTvec,
                                  relativeRvec_cam[n-1],
                                  relativeTvec_cam[n-1]);
                allFrame[n-1]->addEdgeConstrain(n,
                                               relativeRvec_cam[n-1],
                                               relativeTvec_cam[n-1]);
            }
            
        }
        //=============draw map=============================================
        Mat tempAccumRvec, tempAccumTvec;
        for(int n = MAX(0, (int)allFrame.size()-localTimes); n < allFrame.size(); n++) {

            getAccumulateMotion(relativeRvec_cam, relativeTvec_cam,
                                0, n, tempAccumRvec, tempAccumTvec);
            allFrame[n]->setWrdTransVectorAndTransScenePts(tempAccumRvec,tempAccumTvec);
        }
        Eigen::Affine3d curTrans =  Eigen::Affine3d::Identity();
        Eigen::Affine3d curCam = vectorToTransformation(tempAccumRvec, tempAccumTvec);

        mapviewer.addCamera(curCam);
        mapviewer.jointToMap(mapviewer.pointToPointCloud(map.getAllMapPoints()), 
                             curTrans);

        *cloud = mapviewer.entireMap;

        viewer.showCloud(cloud);
        if(waitKey(5) == 27){};
        //============ prepare next loop ================================
        refFrame = allFrame[MAX(0, (int)allFrame.size()-localTimes-1 )];
        prevFrame = allFrame.back();

        deleteFrame++;
        if(deleteFrame > 15 && deleteFrame < 1570) {
            allFrame[deleteFrame]->releaseMemory();
        }
    }

//============== end of main loop =========================================================================
    ofstream outfile;
    outfile.open(savePathBefore);
    //evaluate before global BA
    for(int n = 0; n < dataLength; n++){
        // Mat accumRvec, accumTvec;
        getAccumulateMotion(relativeRvec_cam, relativeTvec_cam,
                            0, n,
                            accumRvec, accumTvec);

        // double diff1 = allFrame[n]->worldRvec.at<double>(0,0) - accumRvec.at<double>(0,0);
        // cout << setprecision(15) <<diff1/accumRvec.at<double>(0,0) * 100.0 <<"\%" <<"  ";

        // double diff2 = allFrame[n]->worldRvec.at<double>(1,0) - accumRvec.at<double>(1,0);
        // cout << setprecision(15)<< diff2/accumRvec.at<double>(1,0) * 100.0 <<"\%" <<"  ";

        // double diff3 = allFrame[n]->worldRvec.at<double>(2,0) - accumRvec.at<double>(2,0);
        // cout << setprecision(15)<< diff3/accumRvec.at<double>(2,0) * 100.0 <<"\%" <<"  ";

        // double diffx = allFrame[n]->worldTvec.at<double>(0,0) - accumTvec.at<double>(0,0);
        // cout << setprecision(15)<< diffx/accumTvec.at<double>(0,0) * 100.0 <<"\%" <<"  ";

        // double diffy = allFrame[n]->worldTvec.at<double>(1,0) - accumTvec.at<double>(1,0);
        // cout << setprecision(15)<< diffy/accumTvec.at<double>(1,0) * 100.0 <<"\%" <<"  ";

        // double diffz = allFrame[n]->worldTvec.at<double>(2,0) - accumTvec.at<double>(2,0);
        // cout << setprecision(15)<< diffz/accumTvec.at<double>(2,0) * 100.0 <<"\%" <<endl;

        Eigen::Affine3d curPose = vectorToTransformation(accumRvec, accumTvec);
        Eigen::Affine3d invPose = curPose.inverse();

        for(int r = 0; r < 3; r++){
            for(int c = 0; c < 4; c++){
                outfile << invPose(r,c) << " ";
            }
        }
        outfile << endl;
    }
    outfile.close();

    outfile.open(savePath);

    //======== test: manually add loop ======================
    // cout << endl<<endl<<"manually adding loops..." << endl<<endl;

    // cout << endl<<endl<<"manually adding loops..." << endl<<endl;
    // for(int out = 0; out < 10; out ++) {
    //     for(int n = 1574; n < 1580; n++) {
    //         allFrame[n]->matchFrame(allFrame[out]);
    //         allFrame[n]->manageMapPoints(allFrame[out]);
    //         allFrame[n]->addEdgeConstrain(allFrame[out]->frameID, 
    //                                         allFrame[n]->rvec,
    //                                         allFrame[n]->tvec);
    //         cout << "from frame: " << allFrame[n]->frameID << " to: " << allFrame[out]->frameID << endl;
    //         cout << allFrame[out]->tvec.at<double>(0,0) << "  " << allFrame[out]->tvec.at<double>(1,0) << "  "<<allFrame[out]->tvec.at<double>(2,0) <<endl;
    //     }
    // }

    // for(int out = 0; out < 5; out ++) {
    //     for(int n = 834; n < 837; n++) {
    //         allFrame[n]->matchFrame(allFrame[out]);
    //         allFrame[n]->manageMapPoints(allFrame[out]);
    //         allFrame[n]->addEdgeConstrain(allFrame[out]->frameID, 
    //                                         allFrame[n]->rvec,
    //                                         allFrame[n]->tvec);
    //         cout << "from frame: " << allFrame[n]->frameID << " to: " << allFrame[out]->frameID << endl;
    //         cout << allFrame[out]->tvec.at<double>(0,0) << "  " << allFrame[out]->tvec.at<double>(1,0) << "  "<<allFrame[out]->tvec.at<double>(2,0) <<endl;
    //     }
    // }

    int fromIdx[] = {835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848, 849, 850};
    int toIdx[] = {    1,   2,   3,   4,   5,   6,   7,   9,  10,  11,  12,  13,  14,  15,  16,  17};
    for(int n = 0; n < 16; n ++) {

        allFrame[fromIdx[n]]->matchFrame(allFrame[toIdx[n]]);
        allFrame[fromIdx[n]]->manageMapPoints(allFrame[toIdx[n]]);
        allFrame[fromIdx[n]]->addEdgeConstrain(allFrame[toIdx[n]]->frameID, 
                                        allFrame[fromIdx[n]]->rvec,
                                        allFrame[fromIdx[n]]->tvec);
        cout << "from frame: " << allFrame[fromIdx[n]]->frameID << " to: " << allFrame[toIdx[n]]->frameID << endl;
        cout << allFrame[toIdx[n]]->tvec.at<double>(0,0) << "  " << allFrame[toIdx[n]]->tvec.at<double>(1,0) << "  "<<allFrame[toIdx[n]]->tvec.at<double>(2,0) <<endl;
        
    }


    
    // for(int out = 0; out < 10; out ++) {
    //     allFrame[out]->matchFrame(allFrame[out+8]);
    //     allFrame[out]->manageMapPoints(allFrame[out+8]);
    //     allFrame[out]->addEdgeConstrain(allFrame[out+8]->frameID, 
    //                                     allFrame[out]->rvec,
    //                                     allFrame[out]->tvec);
    //     cout << "from frame: " << allFrame[out]->frameID << " to: " << allFrame[out+8]->frameID << endl;
    //     cout << allFrame[out]->tvec.at<double>(0,0) << "  " << allFrame[out]->tvec.at<double>(1,0) << "  "<<allFrame[out]->tvec.at<double>(2,0) <<endl;
    // }

    cout<<"creating pose graph..." << endl;
    PoseOpt poseOpt;
    for(int n = 0; n < allFrame.size(); n++){
        poseOpt.addNode(allFrame, n);   
    }
    for(int n = 0; n < allFrame.size(); n++){
        poseOpt.addEdge(allFrame, n);
    }
    poseOpt.solve();

    //======== output poses after g2o ==========================================
    for(int n = 0; n < poseOpt.optimizer.vertices().size(); n++){
        g2o::VertexSE3* v = dynamic_cast<g2o::VertexSE3*>(poseOpt.optimizer.vertex(n));
        Eigen::Isometry3d pose = v->estimate();
        for(int r = 0; r < 3; r++){
            for(int c = 0; c < 4; c++){
                outfile << pose(r,c) << "  ";
            }
        }
        outfile << endl;
    }
    outfile.close();

    //======= update poses after g2o ===========================================
    for(int n = 0; n < poseOpt.optimizer.vertices().size(); n++){
        g2o::VertexSE3* v = dynamic_cast<g2o::VertexSE3*>(poseOpt.optimizer.vertex(n));
        Eigen::Isometry3d pose = v->estimate();
        Eigen::Affine3d affinePose;
        for(int r = 0;r < 4; r++){
            for(int c = 0; c < 4; c++){
                affinePose(r,c) = pose(r,c);
            }
        }    
        affinePose = affinePose.inverse();
        transformationToVector(affinePose, allFrame[n]->worldRvec, allFrame[n]->worldTvec);    
    }

    //======== only adjust points===============================================
    cout << "reprojection only adjustment...  ";
    ba.reprojectionOnlyAdjustment(&map, allFrame);
    cout << "  done!" << endl;
    cout << "throwing bad points...";
    for(auto frame : allFrame){
        frame->judgeBadPoints();
    }
    cout <<"   done!" << endl;

    //=========== draw points  ================================
    // convert to Point3f
    Eigen::Affine3d curTrans =  Eigen::Affine3d::Identity();
    
    vector<Point3f> afterBAPoints3f;

    for(set<MapPoint*>::iterator pIt = map.allMapPoints.begin();
                                 pIt!= map.allMapPoints.end();
                                 pIt++){
        if(!(*pIt)->isBad){
            Point3f pt;
            pt.x = (*pIt)->pos.x;
            pt.y = (*pIt)->pos.y;
            pt.z = (*pIt)->pos.z;

            afterBAPoints3f.push_back(pt);
        }
    }
    mapviewer.addMorePoints(mapviewer.pointToPointCloud(afterBAPoints3f,0,100,100), curTrans);
    *cloud = mapviewer.entireMap;
    viewer.showCloud(cloud);
    if(waitKey(5) == 27){};

    //======== global ba ===============================================
    cout << "global bundle adjustment...   ";
    ba.globalBundleAdjustment(&map, allFrame);
    //============== update accumulative transformation ================
    outfile.open("../06afterGlobalBA.txt");
    for(int n = 1; n < allFrame.size(); n++){

        getRelativeMotion(allFrame[n-1]->worldRvec, allFrame[n-1]->worldTvec,
                          allFrame[n]->worldRvec,   allFrame[n]->worldTvec,
                          relativeRvec_cam[n-1],
                          relativeTvec_cam[n-1]);
    }
    Mat tempAccumRvec, tempAccumTvec;
    for(int n = 0; n < allFrame.size(); n++) {

        getAccumulateMotion(relativeRvec_cam, relativeTvec_cam,
                            0, n, tempAccumRvec, tempAccumTvec);
        allFrame[n]->setWrdTransVectorAndTransScenePts(tempAccumRvec,tempAccumTvec);
    }
    //evaluate after global BA
    for(int n = 0; n < dataLength; n++){
        // Mat accumRvec, accumTvec;
        getAccumulateMotion(relativeRvec_cam, relativeTvec_cam,
                            0, n,
                            accumRvec, accumTvec);

        Eigen::Affine3d curPose = vectorToTransformation(accumRvec, accumTvec);
        Eigen::Affine3d invPose = curPose.inverse();

        mapviewer.addCamera(curPose);

        for(int r = 0; r < 3; r++){
            for(int c = 0; c < 4; c++){
                outfile << invPose(r,c) << " ";
            }
        }
        outfile << endl;
    }
    outfile.close();

    //=========== draw points after BA ================================
    // convert to Point3f
    afterBAPoints3f.clear();

    for(set<MapPoint*>::iterator pIt = map.allMapPoints.begin();
                                 pIt!= map.allMapPoints.end();
                                 pIt++) {
        if(!(*pIt)->isBad) {
            Point3f pt;
            pt.x = (*pIt)->pos.x;
            pt.y = (*pIt)->pos.y;
            pt.z = (*pIt)->pos.z;

            afterBAPoints3f.push_back(pt);
        }
    }
    
    mapviewer.addMorePoints(mapviewer.pointToPointCloud(afterBAPoints3f,0,255,0), curTrans);
    *cloud = mapviewer.entireMap;
    viewer.showCloud(cloud);
    if(waitKey(5) == 27){};

    waitKey(0);
}