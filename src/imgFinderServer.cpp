
//  imgFinderServer
//
//  Created by LuDong on 2018/8/28.
//  Copyright © 2018年 LuDong. All rights reserved.
//

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
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
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
	string blockId;
	flann::Index *flann_index;
	int needReload;
	string hashFolder;
	string indexPath;
	string hashPath;

	string mq_ip;	//RabbitMQ parameters.
	int mq_port;
	string mq_username;
	string mq_password;
	string mq_exchange_in;
	string mq_exchange_out;
} R_Params;

double dealTimeLimit = 3.0;		//3 seconds.
double reloadTime = 10 * 60.0;	//10 minutes.
const char *reloadKey = "/reload.dat";
static FILE *file = fopen("log.log", "a");

amqp_connection_state_t init_rabbitmq_consume(R_Params *params) {	//fanoutExchange

	const char *queue_name = params->blockId.c_str();
	const char *ip = params->mq_ip.c_str();
	int port = params->mq_port;
	const char *username = params->mq_username.c_str();
	const char *password = params->mq_password.c_str();
	const char *exchange = params->mq_exchange_in.c_str();

	amqp_connection_state_t conn = amqp_new_connection();

	amqp_socket_t *socket = amqp_tcp_socket_new(conn);
	if (!socket){
		return NULL;
	}

	int rc = amqp_socket_open(socket, ip, port);
	if (rc){
		return NULL;
	}

	amqp_rpc_reply_t rpc_reply = amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, username, password);
	if (rpc_reply.reply_type != AMQP_RESPONSE_NORMAL){
		return NULL;
	}

	if (amqp_channel_open(conn, 1) == NULL) {
		return NULL;
	}
	if (amqp_get_rpc_reply(conn).reply_type != AMQP_RESPONSE_NORMAL) {
		return NULL;
	}

	amqp_table_entry_t entries[1];
	amqp_table_t table;
	table.entries = entries;
	table.num_entries = 1;
	entries[0].key = amqp_cstring_bytes("x-message-ttl");
	entries[0].value.kind = AMQP_FIELD_KIND_I32;
	entries[0].value.value.i32 = 300;

	amqp_queue_declare_ok_t *res = amqp_queue_declare(conn, 1, amqp_cstring_bytes(queue_name), 0, 1, 0, 0, table);
	if (res == NULL) {
		return NULL;
	}
	amqp_queue_bind(conn, 1, amqp_cstring_bytes(queue_name), amqp_cstring_bytes(exchange), amqp_cstring_bytes(""), amqp_empty_table);
	if (amqp_get_rpc_reply(conn).reply_type != AMQP_RESPONSE_NORMAL) {
		return NULL;
	}

	//amqp_basic_consume_ok_t *result = amqp_basic_consume(conn, 1, amqp_cstring_bytes(queue_name), amqp_empty_bytes, 0, 1, 0, amqp_empty_table);
	//if (result == NULL) {
	//	return NULL;
	//}
	return conn;
}

amqp_connection_state_t init_rabbitmq_publish(R_Params *params) {	//callbackExchange

	const char *ip = params->mq_ip.c_str();
	int port = params->mq_port;
	const char *username = params->mq_username.c_str();
	const char *password = params->mq_password.c_str();

	amqp_connection_state_t conn = amqp_new_connection();

	amqp_socket_t *socket = amqp_tcp_socket_new(conn);
	if (!socket){
		return NULL;
	}

	int rc = amqp_socket_open(socket, ip, port);
	if (rc){
		return NULL;
	}

	amqp_rpc_reply_t rpc_reply = amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, username, password);
	if (rpc_reply.reply_type != AMQP_RESPONSE_NORMAL){
		return NULL;
	}

	if (amqp_channel_open(conn, 1) == NULL) {
		return NULL;
	}
	if (amqp_get_rpc_reply(conn).reply_type != AMQP_RESPONSE_NORMAL) {
		return NULL;
	}
	return conn;
}

/***
	srcDir: source dir contains every hashtable and indexMap.
	dstLshPath: destination IndexMap path.
	dstIndexPath: destination IndexMap path.
	return: result code.
*/
int mergeHashTableFromDir(const char *srcDir, const char *dstLshPath, const char *dstIndexPath) {

	int dst_pktCount;
	int ret = loadIndexFile(string(dstIndexPath), dst_pktCount);
	if (ret != 0) {
		return 4;
	}
	flann::Index *dst_flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(dstLshPath), cvflann::FLANN_DIST_HAMMING);

	vector<string> filenames;
	readDirectory(srcDir, filenames, 0);

	char KeyIdsName[200] = { 0 };
	sprintf(KeyIdsName, "%s/keyIds.txt", srcDir);
	FILE *KeyIdsFile = fopen(KeyIdsName, "w");

	for (int i = 0; i<filenames.size(); i++) {
		string filename = filenames[i];
		if (endsWith(filename, "_LSHashTable.dat")) {

			vector<string> splitPaths = splitString(splitString(filename, "_LSHashTable")[0], "/");
			string picId = splitPaths[splitPaths.size() - 1];
			char IndexMap[200] = { 0 };
			sprintf(IndexMap, "%s/%s_IndexMap.dat", srcDir, picId.c_str());

			if (exist_file(IndexMap)) {
				flann::Index *src_flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(filename), cvflann::FLANN_DIST_HAMMING);
				std::vector<std::unordered_map<uint32_t, std::vector<uint32_t> > > buckets;
				src_flann_index->getAllBuckets(buckets);

				//dst_flann_index->rehash();
				dst_flann_index->copyBuckets(buckets, dst_pktCount);
				if (buckets.size()<1) {
					fprintf(KeyIdsFile, "4,%s\n", picId.c_str());
					fflush(KeyIdsFile);
					continue;
				}
				vector<int> src_idx2Imgs;
				vector<unsigned short> src_xPts;
				vector<unsigned short> src_yPts;
				unordered_map<int, int> src_imgKptCounts;

				int src_accIndex;
				ret = loadIndexFile(string(IndexMap), src_idx2Imgs, src_xPts, src_yPts, src_accIndex, src_imgKptCounts);
				if (ret != 0) {
					fprintf(KeyIdsFile, "4,%s\n", picId.c_str());
					fflush(KeyIdsFile);
					continue;
				}
				int targetImgId;

				ret = appendIndexFile(string(dstIndexPath), src_idx2Imgs, src_xPts, src_yPts, targetImgId);
				if (ret != 0) {
					fprintf(KeyIdsFile, "4,%s\n", picId.c_str());
					fflush(KeyIdsFile);
					continue;
				}
				fprintf(KeyIdsFile, "0,%s,%d\n", picId.c_str(), targetImgId);
				fflush(KeyIdsFile);
				src_flann_index->release();
			}
		}
	}

	dst_flann_index->save(dstLshPath);
	dst_flann_index->release();
	fclose(KeyIdsFile);
	return 0;
}

/***
	imgPath: dealing picture path.
	basicHashPath: basic hashtable path. 152B.
	storePath: the path will store hashtable and indexMap of this picture.
	imgInternalId: the id store in http server database.
	return: result code.
*/
int img2hashtable(const char *imgPath, const char *basicHashPath, const char *storePath, const char *imgInternalId) {

	if (!exist_file(imgPath)) {
		return 5;
	}
	Mat image = imread(imgPath);
	if (image.dims != 2) {  //read error.
		return 2;
	}

	flann::Index *flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(basicHashPath), cvflann::FLANN_DIST_HAMMING);

	Ptr<AKAZE> akaze = AKAZE::create();
	akaze->setThreshold(0.002);

	vector<int> idx2Imgs;
	vector<KeyPoint> kpts;
	Mat desc;
	char hashPath[200] = { 0 };
	char indexPath[200] = { 0 };

	sprintf(hashPath, "%s/%s_LSHashTable.dat", storePath, imgInternalId);
	sprintf(indexPath, "%s/%s_IndexMap.dat", storePath, imgInternalId);
	string idx2Imgs_path(indexPath);

	vector<unsigned short> xPts;
	vector<unsigned short> yPts;
	int imgIndex = 0;

	if (image.rows>image.cols) {
		int cols = FIX_SIZE*image.cols / image.rows;
		cv::resize(image, image, cv::Size(cols, FIX_SIZE));
	}
	else {
		int rows = FIX_SIZE*image.rows / image.cols;
		cv::resize(image, image, cv::Size(FIX_SIZE, rows));
	}

	akaze->detectAndCompute(image, noArray(), kpts, desc);
	if (kpts.size()<100) {    //keypoints not enough.
		return 3;
	}

	for (int r = 0; r<kpts.size(); r++) {
		idx2Imgs.push_back(imgIndex);
		xPts.push_back((unsigned short)kpts[r].pt.x);
		yPts.push_back((unsigned short)kpts[r].pt.y);
		flann_index->addData(desc.data + r*desc.step[0], (int)idx2Imgs.size());
	}

	flann_index->save(hashPath);
	flann_index->release();
	writeIndexFile(idx2Imgs_path, idx2Imgs, xPts, yPts, imgIndex);

	return 0;
}

void *retrieveActionThread(void *a) {

	int *topK = (int *)malloc(100 * 1000 * sizeof(int));
	unsigned char* data = (unsigned char*)malloc(10 * 1024 * 1024);

	R_Params *params = (R_Params *)a;
	vector<int> idx2Imgs = params->idx2Imgs;
	vector<unsigned short> xPts = params->xPts;
	vector<unsigned short> yPts = params->yPts;
	unordered_map<int, int> imgKptCounts = params->imgKptCounts;
	int accIndex = params->accIndex;
	const char *blockId = params->blockId.c_str();
	const char *exchange_out = params->mq_exchange_out.c_str();
	flann::Index *flann_index = params->flann_index;

	amqp_connection_state_t consume_conn = init_rabbitmq_consume(params);
	if (consume_conn == NULL) {
		printf("init_rabbitmq_consume FAILED..\n");
		return NULL;
	}

	amqp_connection_state_t publish_conn = init_rabbitmq_publish(params);
	if (publish_conn == NULL) {
		printf("init_rabbitmq_publish FAILED..\n");
		return NULL;
	}
	timer reloadTimer;
	reloadTimer.restart();

	while (1) {

	timeout:
		//Decide if need reload hashtable.
		if (params->needReload) {
			if (!exist_file(params->hashFolder + reloadKey)) {
				params->needReload = 0;
			}
			else {
				double elapsed = reloadTimer.elapsed_s();
				if (elapsed > reloadTime) {
					idx2Imgs.clear();
					xPts.clear();
					yPts.clear();
					imgKptCounts.clear();
					loadIndexFile(params->indexPath, idx2Imgs, xPts, yPts, accIndex, imgKptCounts);
					flann_index->release();
					flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(params->hashPath), cvflann::FLANN_DIST_HAMMING);
					reloadTimer.restart();
				}
			}
		}

		// Get Message from MQ
		amqp_rpc_reply_t rpc_reply;
		do {
			rpc_reply = amqp_basic_get(consume_conn, 1, amqp_cstring_bytes(blockId), 1);
			amqp_maybe_release_buffers(consume_conn);
		} while (rpc_reply.reply_type == AMQP_RESPONSE_NORMAL &&
			rpc_reply.reply.id == AMQP_BASIC_GET_EMPTY_METHOD);
		if (!(rpc_reply.reply_type == AMQP_RESPONSE_NORMAL || rpc_reply.reply.id == AMQP_BASIC_GET_OK_METHOD)) {
			printf("Error: --amqp_basic_get-- error.\n");
			goto timeout;
		}

		amqp_message_t message;
		rpc_reply = amqp_read_message(consume_conn, 1, &message, 0);
		if (rpc_reply.reply_type != AMQP_RESPONSE_NORMAL) {
			printf("Error: --amqp_read_message-- error.\n");
			goto timeout;
		}

		memset(data, 0, 1024 * 1024);
		memcpy(data, message.body.bytes, message.body.len);
		amqp_destroy_message(&message);
		amqp_maybe_release_buffers(consume_conn);

		timer dealTimer;
		dealTimer.restart();

		cJSON *json = cJSON_Parse((const char *)data);
		if (json == NULL) {
			printf("Not Json Message:  %s\n", data);
			goto timeout;
		}
		char taskId[100];
		cJSON *taskSub = cJSON_GetObjectItem(json, "taskId");
		if (taskSub == NULL) {
			printf("Not Json Message:  %s\n", data);
			goto timeout;
		}
		strcpy(taskId, taskSub->valuestring);
		cJSON *bytesSub = cJSON_GetObjectItem(json, "bytes");
		if (bytesSub == NULL) {
			printf("Not Json Message:  %s\n", data);
			goto timeout;
		}
		char *bytes = bytesSub->valuestring;
		
		memset(data, 0, 1024 * 1024);
		int len = base64_decode(string(bytes), data);
		printf("taskId = %s, len = %d, blockId = %s\n", taskId, len, blockId);
		cJSON_Delete(json);

		// Parse Message
		memset(topK, 0, 100 * 1000 * sizeof(int));
		int ptr = 0;
		int index = 0;

		unsigned short kptsCount = 0;
		memcpy(&kptsCount, data, sizeof(unsigned short));
		ptr += sizeof(unsigned short);

		timer dealTimer;
		dealTimer.restart();

		//Begin Retrieve Hashtable
		map<int, Point2f> mapCoords;
		map<int, int> topKCount;
		unordered_map<int, int> matchMaps;
		matchMaps.reserve(1000 * 100);

		vector<Point2f> vecCoords;
		int k = 0;
		for (int i = 0; i<kptsCount; i++) {

			matchMaps.clear();

			vector<uint32_t> miniHashVals;

			miniHashVals.push_back(*((uint32_t *)(data + ptr)));
			ptr += sizeof(uint32_t);

			miniHashVals.push_back(*((uint32_t *)(data + ptr)));
			ptr += sizeof(uint32_t);

			miniHashVals.push_back(*((uint32_t *)(data + ptr)));
			ptr += sizeof(uint32_t);

			miniHashVals.push_back(*((uint32_t *)(data + ptr)));
			ptr += sizeof(uint32_t);

			miniHashVals.push_back(*((uint32_t *)(data + ptr)));
			ptr += sizeof(uint32_t);

			unsigned short ptx = *((unsigned short *)(data + ptr));
			ptr += sizeof(unsigned short);
			unsigned short pty = *((unsigned short *)(data + ptr));
			ptr += sizeof(unsigned short);

			flann_index->getNeighborsByHash(miniHashVals, matchMaps, topK, index, 3);

			for (; k<index; k++) { //multi to multi, topK[k] = index of point in hashtable
				mapCoords[topK[k]] = Point2f(ptx, pty);
				topKCount[idx2Imgs[topK[k] - 1]]++;
			}
			double elapsed = dealTimer.elapsed_s();
			if (elapsed > dealTimeLimit) {
				goto timeout;
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
			double elapsed = dealTimer.elapsed_s();
			if (elapsed > dealTimeLimit) {
				goto timeout;
			}
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
			float prob = (float)ransacCount / imgKptCounts[iter->first];
			if (prob>0.03 && ransacCount>15) {
				candidates[iter->first] = prob;

			}
		}
		double elapsed = dealTimer.elapsed_s();
		if (elapsed > dealTimeLimit) {
			goto timeout;
		}

		//Return Result
		if (candidates.size()>0) {
			vector<pair<int, float>> candidates_vec(candidates.begin(), candidates.end());
			sort(candidates_vec.begin(), candidates_vec.end(), comp_by_value);

			amqp_basic_properties_t props;
			props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
			props.content_type = amqp_cstring_bytes("text/plain");
			props.delivery_mode = 1;
			char body[500] = { 0 };
			sprintf(body, "{\"code\": 0,\"prob\":%f,\"blockId\":\"%s\",\"msg\":\"ok\",\"keyId\": %d,\"taskId\":\"%s\"}", candidates_vec[0].second, blockId, candidates_vec[0].first, taskId);
			amqp_basic_publish(publish_conn, 1, amqp_cstring_bytes(exchange_out), amqp_cstring_bytes(""), 0, 0, &props, amqp_cstring_bytes(body));
			 printf("body = %s\n", body);
		}
		else {
			amqp_basic_properties_t props;
			props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
			props.content_type = amqp_cstring_bytes("text/plain");
			props.delivery_mode = 1;
			char body[500] = { 0 };
			sprintf(body, "{\"code\": -1,\"prob\":0,\"blockId\":\"%s\",\"msg\":\"null\",\"keyId\": -1,\"taskId\":\"%s\"}", blockId, taskId);
			amqp_basic_publish(publish_conn, 1, amqp_cstring_bytes(exchange_out), amqp_cstring_bytes(""), 0, 0, &props, amqp_cstring_bytes(body));
			 printf("body = %s\n", body);
		}
		elapsed = dealTimer.elapsed_s();
		amqp_maybe_release_buffers(publish_conn);
	}

	free(data);
	free(topK);
	return NULL;
}

#define n_threads 10

void showHelp() {
	printf("Help:\r\n");
	printf("--mergeFromDir srcDir, dstLshPath, dstIndexPath\n");
	printf("--img2hashtable imgPath, hashPath, storePath, imgInternalId\n");
	printf("--serve jsonPath\r\n");
}

void hash2Thread(string folder, R_Params *params, char *machineId) {

	string indexPath = folder + "/DayIndexMap.dat";
	string hashPath = folder + "/DayLSHashTable.dat";

	if (exist_file(indexPath) && exist_file(hashPath)) {

		vector<int> idx2Imgs;
		vector<unsigned short> xPts;
		vector<unsigned short> yPts;
		int accIndex;
		unordered_map<int, int> imgKptCounts;

		loadIndexFile(indexPath, idx2Imgs, xPts, yPts, accIndex, imgKptCounts);
		flann::Index *flann_index = new flann::Index(Mat(cv::Size(61, 1), CV_8U), flann::SavedIndexParams(hashPath), cvflann::FLANN_DIST_HAMMING);

		
		params->idx2Imgs = idx2Imgs;
		params->xPts = xPts;
		params->yPts = yPts;
		params->accIndex = accIndex;
		params->imgKptCounts = imgKptCounts;
		params->flann_index = flann_index;
		params->hashFolder = folder;
		params->hashPath = hashPath;
		params->indexPath = indexPath;
		params->needReload = 1;
		vector<string> tmp = splitString(folder, "/");
		params->blockId = string(machineId) + "_" + tmp[tmp.size() - 1];	//blockId = machineId_blockId

		pthread_t threads;
		pthread_create(&threads, NULL, retrieveActionThread, (void*)params);
	}
}

int main(int argc, const char * argv[]) {

	if (!strcmp(argv[1], "--serve")) {  //server for retrieving, registering.
		if (argc != 3) {
			showHelp();
			return 1;
		}
		char *jsonConfig = json_loader(argv[2]);
		
		cJSON *root = cJSON_Parse(jsonConfig);
		if (!root) {
			printf("Error before: [%s]\n", cJSON_GetErrorPtr());
			return 1;
		}
		
		char machineId[100] = { 0 };
		char hashFolder[500] = { 0 };
		char mq_ip[100] = { 0 };
		int mq_port = 0;
		char mq_uesrname[100] = { 0 };
		char mq_password[100] = { 0 };
		char mq_exchange_in[100] = { 0 };
		char mq_exchange_out[100] = { 0 };

		strcpy(machineId, cJSON_GetObjectItem(root, "MachineId")->valuestring);
		strcpy(hashFolder, cJSON_GetObjectItem(root, "HashFolder")->valuestring);
		strcpy(mq_ip, cJSON_GetObjectItem(root, "MQ_IP")->valuestring);
		mq_port = cJSON_GetObjectItem(root, "MQ_Port")->valueint;
		strcpy(mq_uesrname, cJSON_GetObjectItem(root, "MQ_Username")->valuestring);
		strcpy(mq_password, cJSON_GetObjectItem(root, "MQ_Password")->valuestring);
		strcpy(mq_exchange_in, cJSON_GetObjectItem(root, "MQ_Exchange_in")->valuestring);
		strcpy(mq_exchange_out, cJSON_GetObjectItem(root, "MQ_Exchange_out")->valuestring);
		
		if (!exist_file(hashFolder)) {
			printf("HashFolder not exist.\n");
			return 1;
		}
		vector<string> subfolders;
		readDirectory(hashFolder, subfolders, 1);
		
		for (int i = 0; i < subfolders.size(); i++) {	//visit all subfolder, (DayIndexMap.dat, DayLSHashTable.dat)
			string folder = subfolders[i];
			R_Params *params = new R_Params();

			params->mq_ip = string(mq_ip);
			params->mq_port = mq_port;
			params->mq_username = string(mq_uesrname);
			params->mq_password = string(mq_password);
			params->mq_exchange_in = string(mq_exchange_in);
			params->mq_exchange_out = string(mq_exchange_out);

			hash2Thread(folder, params, machineId);
		}
		while (1) {	// sleep for every reloadTime, detect if a new hashtable generated.
			sleep(reloadTime);
			vector<string> curSubfolders;
			readDirectory(hashFolder, curSubfolders, 1);

			for (int i = 0; i < curSubfolders.size(); i++) {
				vector<string>::iterator ret = std::find(subfolders.begin(), subfolders.end(), curSubfolders[i]);
				if (ret == subfolders.end()) {	//not found, indicate a new hashtable

					R_Params *params = new R_Params();

					params->mq_ip = string(mq_ip);
					params->mq_port = mq_port;
					params->mq_username = string(mq_uesrname);
					params->mq_password = string(mq_password);
					params->mq_exchange_in = string(mq_exchange_in);
					params->mq_exchange_out = string(mq_exchange_out);

					hash2Thread(curSubfolders[i], params, machineId);
					subfolders.push_back(curSubfolders[i]);
				}
			}
		}
	}
	else if (!strcmp(argv[1], "--mergeFromDir")) {  //copy hashtable's data to other hashtable, from dir.
		if (argc != 5) {
			showHelp();
			return 1;
		}
		int ret = mergeHashTableFromDir(argv[2], argv[3], argv[4]);
		return ret;
	}
	else if (!strcmp(argv[1], "--img2hashtable")) {  //extract features of image to store in hashtable.
		if (argc != 6) {
			showHelp();
			return 1;
		}
		int ret = img2hashtable(argv[2], argv[3], argv[4], argv[5]);
		return ret;
	}
	else {
		showHelp();
		return 1;
	}

	return 0;
}


