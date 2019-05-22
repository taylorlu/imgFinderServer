
//  imgFinderServer
//
//  Created by LuDong on 2018/8/28.
//  Copyright © 2018年 LuDong. All rights reserved.
//
#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <dirent.h>
#include <opencv2/flann/lsh_index.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#include <unistd.h>
#include "utils.h"

#define FIX_SIZE 640

using namespace cv;
using namespace std;

bool comp_by_value(pair<int, float> &p1, pair<int, float> &p2){
	return p1.second > p2.second;
}
bool comp_by_value_int(pair<int, int> &p1, pair<int, int> &p2){
	return p1.second > p2.second;
}

typedef struct {

	vector<int> idx2Imgs;
	vector<unsigned short> xPts;
	vector<unsigned short> yPts;
	unordered_map<int, int> imgKptCounts;
	int accIndex;
	flann::Index *flann_index;
} R_Params;

/***
	imagePaths: picture path.
	basicHashPath: basic hashtable path. 92B.
	saveFolder: the path where the hashtable will store.
	return: result code.
*/
int scanImages(vector<string> imagePaths, const char *basicHashPath, const char *saveFolder) {

	flann::Index *flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(basicHashPath), cvflann::FLANN_DIST_HAMMING);

	Ptr<AKAZE> akaze = AKAZE::create();
	akaze->setThreshold(0.001);
	int hashFileIndex = 0;
	int imgIndex = 0;
	int hashImageSize = 1000;
	char hashPath[200] = { 0 };
	string indexPathSave;
	string imagePathSave = string(saveFolder) +string("/")+ string("ImgPath");;
	vector<int> idx2Imgs;
	vector<unsigned short> xPts;
	vector<unsigned short> yPts;

	for (vector<string>::iterator iter = imagePaths.begin(); iter != imagePaths.end();) {
		Mat image = imread(iter->c_str(), IMREAD_GRAYSCALE);
		if (image.dims != 2) {  //read error.
			printf("!! read error %s\n", iter->c_str());
			imagePaths.erase(iter);
			continue;
		}

		if (image.rows>image.cols) {
			int cols = FIX_SIZE*image.cols / image.rows;
			cv::resize(image, image, cv::Size(cols, FIX_SIZE));
		}
		else {
			int rows = FIX_SIZE*image.rows / image.cols;
			cv::resize(image, image, cv::Size(FIX_SIZE, rows));
		}

		vector<KeyPoint> kpts;
		Mat desc;
		akaze->detectAndCompute(image, noArray(), kpts, desc);
		printf("%d, ", kpts.size());
		// if (kpts.size()<100) {    //keypoints not enough.
			// imagePaths.erase(iter);
		// 	continue;
		// }
		for (int r = 0; r<kpts.size(); r++) {
			idx2Imgs.push_back(imgIndex);
			xPts.push_back((unsigned short)kpts[r].pt.x);
			yPts.push_back((unsigned short)kpts[r].pt.y);
			flann_index->addData(desc.data + r*desc.step[0], (int)idx2Imgs.size());
		}

		if((imgIndex+1)%hashImageSize==0) {	//Store the hashtable of 1000(hashImageSize) images
			sprintf(hashPath, "%s/%d.LSHashTable", saveFolder, hashFileIndex);
			indexPathSave = string(saveFolder) +string("/")+ to_string(hashFileIndex) + string(".IndexMap");
			printf("saved %s\n", hashPath);
			flann_index->save(hashPath);
			flann_index->release();
			writeIndexFile(indexPathSave, idx2Imgs, xPts, yPts, imgIndex);
			idx2Imgs.clear();
			xPts.clear();
			yPts.clear();
			flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(basicHashPath), cvflann::FLANN_DIST_HAMMING);
			hashFileIndex++;
		}
		imgIndex++;
		iter++;
	}
	if((imgIndex+1)%hashImageSize!=0) {	//Store the hashtable of 1000(hashImageSize) images
		sprintf(hashPath, "%s/%d.LSHashTable", saveFolder, hashFileIndex);
		indexPathSave = string(saveFolder) +string("/")+ to_string(hashFileIndex) + string(".IndexMap");

		printf("saved %s\n", hashPath);
		flann_index->save(hashPath);
		writeIndexFile(indexPathSave, idx2Imgs, xPts, yPts, imgIndex);
		idx2Imgs.clear();
		xPts.clear();
		yPts.clear();
	}
	writeImgPaths(imagePathSave, imagePaths);
	flann_index->release();
	return 0;
}

/***
	imagePath: picture path.
	basic_flann_index: basic hashtable path. 92B.
	params: contains info of searched hashtable.
	return: image id.
*/
int retrieveFunction(const char *imagePath, vector<string> imgPathVector, R_Params *params, flann::Index *basic_flann_index) {

	vector<unsigned short> xPts = params->xPts;
	vector<unsigned short> yPts = params->yPts;
	vector<int> idx2Imgs = params->idx2Imgs;
	unordered_map<int, int> imgKptCounts = params->imgKptCounts;
	flann::Index *flann_index = params->flann_index;

	int *topK = (int *)malloc(100 * 1000 * sizeof(int));

	// Parse Message
	memset(topK, 0, 100 * 1000 * sizeof(int));
	int ptr = 0;
	int index = 0;

	//Begin Retrieve Hashtable
	map<int, Point2f> mapCoords;
	map<int, int> topKCount;
	unordered_map<int, int> matchMaps;
	matchMaps.reserve(1000 * 100);

	Mat image = imread(imagePath);
	if (image.rows>image.cols) {
		int cols = FIX_SIZE*image.cols / image.rows;
		cv::resize(image, image, cv::Size(cols, FIX_SIZE));
	}
	else {
		int rows = FIX_SIZE*image.rows / image.cols;
		cv::resize(image, image, cv::Size(FIX_SIZE, rows));
	}
	Ptr<AKAZE> akaze = AKAZE::create();
	akaze->setThreshold(0.0001);
	vector<KeyPoint> kpts;
	Mat desc;
	akaze->detectAndCompute(image, noArray(), kpts, desc);

	int k = 0;
	for (int i = 0; i<kpts.size(); i++) {

		matchMaps.clear();

		uchar *lineData = (uchar *)(desc.data + i*desc.step[0]);
		vector<uint32_t> miniHashVals;
		basic_flann_index->getHashVal(lineData, miniHashVals);

		unsigned short ptx = (unsigned short)kpts[i].pt.x;
		unsigned short pty = (unsigned short)kpts[i].pt.y;

		flann_index->getNeighborsByHash(miniHashVals, matchMaps, topK, index, 3);

		for (; k<index; k++) { //multi to multi, topK[k] = index of point in hashtable
			mapCoords[topK[k]] = Point2f(ptx, pty);
			topKCount[idx2Imgs[topK[k] - 1]]++;
		}
	}

	//Sort Probability
	vector<pair<int, int>> name_score_vec(topKCount.begin(), topKCount.end());
	sort(name_score_vec.begin(), name_score_vec.end(), comp_by_value_int);

	map<int, float> candidates;
	int cc = 0;
	for (vector<pair<int, int>>::iterator iter = name_score_vec.begin(); iter != name_score_vec.end(); ++iter) {

		if (cc>5) {
			break;
		}
		cc++;
		vector<Point2f> p01;
		vector<Point2f> p02;
		for (int i = 0; i<index; i++) {
			if (idx2Imgs[topK[i] - 1] == iter->first) {
				p01.push_back(mapCoords[topK[i]]);
				p02.push_back(Point2f(xPts[topK[i] - 1], yPts[topK[i] - 1]));
			}
		}

		vector<uchar> RansacStatus;
		Mat Fundamental = findFundamentalMat(p01, p02, RansacStatus, FM_RANSAC);

		int ransacCount = 0;
		for (int i = 0; i<RansacStatus.size(); i++) {
			if (RansacStatus[i] != 0) {
				ransacCount++;
			}
		}
		if(ransacCount>0) {
			candidates[iter->first] = ransacCount;
		}
		// float prob = (float)ransacCount / imgKptCounts[iter->first];
		// if (prob>0.03 && ransacCount>5) {
		// 	candidates[iter->first] = prob;

		// }
	}

	free(topK);

	//Return Result
	if (candidates.size()>0) {
		vector<pair<int, float>> candidates_vec(candidates.begin(), candidates.end());
		sort(candidates_vec.begin(), candidates_vec.end(), comp_by_value);
		for(vector<pair<int, float>>::iterator iter=candidates_vec.begin(); iter!=candidates_vec.end(); iter++) {
			printf("%s:%s\n", imagePath, imgPathVector[iter->first].c_str());
		}
		return candidates_vec[0].first;
	}

	return -1;
}

void showHelp() {
	printf("Help:\r\n");
	printf("--scanImages imagePathListFile, HashSaveFolder\n");
	printf("--retrieve HashSaveFolder, imagePath\r\n");
}

R_Params *loadHash(string body) {

	string indexPathSave = body + "IndexMap";
	string hashPathSave = body + "LSHashTable";
	printf("%s\n", indexPathSave.c_str());
	printf("%s\n", hashPathSave.c_str());

	if (exist_file(indexPathSave) && exist_file(hashPathSave)) {

		vector<int> idx2Imgs;
		vector<unsigned short> xPts;
		vector<unsigned short> yPts;
		int accIndex;
		unordered_map<int, int> imgKptCounts;

		loadIndexFile(indexPathSave, idx2Imgs, xPts, yPts, accIndex, imgKptCounts);
		flann::Index *flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(hashPathSave), cvflann::FLANN_DIST_HAMMING);

		R_Params *params = new R_Params();
		params->idx2Imgs = idx2Imgs;
		params->xPts = xPts;
		params->yPts = yPts;
		params->imgKptCounts = imgKptCounts;
		params->accIndex = accIndex;
		params->flann_index = flann_index;
		return params;
	}
	return NULL;
}

int main(int argc, const char * argv[]) {

	if (!strcmp(argv[1], "--retrieve")) {  //server for retrieving, registering.
		if (argc != 4) {
			showHelp();
			return 1;
		}

		char *hashFolder = (char *)argv[2];
		char *imagePath = (char *)argv[3];
		vector<string> imgPathVector;
		string imgPathSave = string(hashFolder) + "/ImgPath";
		loadImgPaths(imgPathSave, imgPathVector);

		vector<string> filenames;
		readDirectory(hashFolder, filenames, 0);
		list<R_Params *> params_list;
		for (int i = 0; i < filenames.size(); i++) {	//visit all subfolder, (DayIndexMap.dat, DayLSHashTable.dat)
			string filename = filenames[i];
			if(endsWith(filename, "LSHashTable")) {
				string body = filename.substr(0, (filename.length()-11));
				R_Params *params = loadHash(body);
				params_list.push_back(params);
			}
			
		}
		flann::Index *basic_flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams("BASIC_Hash.dat"), cvflann::FLANN_DIST_HAMMING);

		for(list<R_Params *>::iterator iter = params_list.begin(); iter != params_list.end(); ++iter) {
			R_Params *params = *iter;
			retrieveFunction(imagePath, imgPathVector, params, basic_flann_index);
		}

	}
	else if (!strcmp(argv[1], "--scanImages")) {  //extract features of image to store in hashtable.
		if (argc != 4) {
			showHelp();
			return 1;
		}
		vector<string> imagePaths;
		string strLine;
		ifstream file(argv[2]);
		while(getline(file, strLine)) {
			size_t n = strLine.find_last_not_of( " \r\n\t" );
			if(n != string::npos) {
			    strLine.erase(n + 1, strLine.size() - n);
			}
			imagePaths.push_back(strLine);
		}
		int ret = scanImages(imagePaths, "BASIC_Hash.dat", argv[3]);
		return ret;
	}
	else {
		showHelp();
		return 1;
	}

	return 0;
}


