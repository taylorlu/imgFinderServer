import sys
import os
import requests
import json
import subprocess
import datetime
import time
import threading
import shutil

err_dict = dict()
err_dict[0] = 'Success.'
err_dict[1] = 'Unknow Error.'
err_dict[2] = 'Picture format not supported.'
err_dict[3] = 'Picture features not enough.'
err_dict[4] = 'Hashtable copy error.'
err_dict[5] = 'Picture path not exist.'

machineId = ''
tmpBasePath = ''
hashFolder = ''
curMergeFolder = ''
basicHashPath = 'BASIC_Hash.dat'
basicIndexPath = 'BASIC_Index.dat'
n_threads = 20

MAX_HASH_SIZE = 90*1000*1000
taskUrl = ''
responseUrl = ''
renewFolderTimerGap = 60*1
mergeTimerGap = 60*1.5
mutex = threading.Lock()
isWorking = True

def timeGapNewFolderList(baseFolder, gap = renewFolderTimerGap):   #latest folders, reuse to extract features.
    curTime = int(time.time())
    
    validList = []
    folders = os.listdir(baseFolder)
    for folder in folders:
        folderTime = datetime.datetime.strptime(folder, "%Y%m%d%H%M%S")
        folderTime = int(time.mktime(folderTime.timetuple()))
        if(curTime-folderTime<gap):
            validList.append(os.path.join(baseFolder, folder))
    return validList

def timeGapOldFolderList(baseFolder, gap = mergeTimerGap):   #past folders, will be deleted by mergeHashTable.
    curTime = int(time.time())
    
    validList = []
    folders = os.listdir(baseFolder)
    for folder in folders:
        folderTime = datetime.datetime.strptime(folder, "%Y%m%d%H%M%S")
        folderTime = int(time.mktime(folderTime.timetuple()))
        if(curTime-folderTime>gap):
            validList.append(os.path.join(baseFolder, folder))
    return validList

def extractFeatureThread():
    global machineId, tmpBasePath, hashFolder, curMergeFolder, taskUrl, responseUrl

    while(1):
        try:
            res = requests.post(taskUrl)
            hjson = json.loads(res.text)
        except Exception, e:
            print(str(e))
            continue
        
        if(hjson['code']==0):
            picId = hjson['info']['id']
            picPath = hjson['info']['picPath']
            
            curTime = time.strftime("%Y%m%d%H%M%S", time.localtime())
            newDir = os.path.join(tmpBasePath, curTime) #create new folder
            
            mutex.acquire()
            validList = timeGapNewFolderList(tmpBasePath)
            if(len(validList)==0):
                if(not os.path.exists(newDir)):
                    os.mkdir(newDir)
            else:
                newDir = validList[0]
            mutex.release()

            cmdLine = './imgFinderServer --img2hashtable '+ picPath +' '+ basicHashPath +' '+ newDir +' '+ str(picId)
            print (cmdLine)
            p = subprocess.Popen(cmdLine, shell=True, close_fds=True, stdout=subprocess.PIPE)
            p.communicate()
            ret = p.returncode

            if(ret!=0):
                res_dict = dict()
                res_dict['status'] = -ret
                if(err_dict.has_key(ret)):
                    res_dict['msg'] = err_dict[ret]
                else:
                    res_dict['msg'] = "Error for execute: " + cmdLine
                res_dict['id'] = picId
                _, blockId = os.path.split(curMergeFolder)
                res_dict['blockCode'] = machineId+ '_' +blockId
                res_dict['keyId'] = ''
                res_dict['time'] = time.strftime("%Y%m%d%H%M%S", time.localtime())
                jsonstr = json.dumps(res_dict)
                try:
                    res = requests.post(responseUrl, jsonstr)
                except Exception, e:
                    print(str(e))

def mergeNewFolder():
    global curMergeFolder

    reloadKey = os.path.join(curMergeFolder, 'reload.dat')
    os.system('rm -rf '+ reloadKey)

    curTime = time.strftime("%Y%m%d%H%M%S", time.localtime())
    curMergeFolder = os.path.join(hashFolder, curTime)
    if(not os.path.exists(curMergeFolder)):    #subfolders
        os.mkdir(curMergeFolder)
    reloadKey = open(os.path.join(curMergeFolder, 'reload.dat'), 'w')
    reloadKey.close()

    storeHashPath = os.path.join(curMergeFolder, 'DayLSHashTable.dat')
    storeIndexPath = os.path.join(curMergeFolder, 'DayIndexMap.dat')

    if(not os.path.isfile(storeHashPath)):  # copy 'BASIC_Hash.dat' to storeHashPath.
        shutil.copyfile(basicHashPath, storeHashPath)
    if(not os.path.isfile(storeIndexPath)):
        shutil.copyfile(basicIndexPath, storeIndexPath)

    return (storeHashPath, storeIndexPath)

def mergeHashTable():
    global machineId, tmpBasePath, hashFolder, curMergeFolder

    timer = threading.Timer(mergeTimerGap, mergeHashTable)

    storeHashPath = os.path.join(curMergeFolder, 'DayLSHashTable.dat')
    storeIndexPath = os.path.join(curMergeFolder, 'DayIndexMap.dat')
    if(os.path.exists(storeHashPath) and int(os.path.getsize(storeHashPath))<MAX_HASH_SIZE):  #decide if cur hashtable > 90M
        pass
    else:
        storeHashPath, storeIndexPath = mergeNewFolder()

    validList = timeGapOldFolderList(tmpBasePath)
    for validFolder in validList: #timestamp folder

        if(int(os.path.getsize(storeHashPath))>MAX_HASH_SIZE):
            storeHashPath, storeIndexPath = mergeNewFolder()

        cmdLine = './imgFinderServer --mergeFromDir '+ validFolder +' '+ storeHashPath +' '+ storeIndexPath
        print(cmdLine)
        p = subprocess.Popen(cmdLine, shell=True, close_fds=True, stdout=subprocess.PIPE)
        p.communicate()

        keyIds = open(os.path.join(validFolder, 'keyIds.txt'))
        line = keyIds.readline().strip()
        while(len(line)>2):

            ret = int(line.split(',')[0])
            picId = line.split(',')[1]
            res_dict = dict()
            res_dict['keyId'] = ''
            res_dict['id'] = picId

            if(ret!=0):
                if(err_dict.has_key(ret)):
                    res_dict['msg'] = err_dict[ret]
                else:
                    res_dict['msg'] = "Error for execute: " + cmdLine
            else:
                res_dict['msg'] = err_dict[ret]
                targetImgId = line.split(',')[2]
                res_dict['keyId'] = targetImgId

            res_dict['status'] = -ret
            _, blockId = os.path.split(curMergeFolder)
            res_dict['blockCode'] = machineId+ '_' +blockId
            res_dict['time'] = time.strftime("%Y%m%d%H%M%S", time.localtime())

            jsonstr = json.dumps(res_dict)
            for retry in range(3):
                try:
                    res = requests.post(responseUrl, jsonstr)
                    hjson = json.loads(res.text)
                except Exception, e:
                    print(str(e))
                    continue
                if(hjson['code']==0):
                    break
            print(jsonstr)

            line = keyIds.readline().strip()
        os.system('rm -rf '+validFolder)

    timer.start()

def main(argv):
    global machineId, tmpBasePath, hashFolder, curMergeFolder, taskUrl, responseUrl

    jsonConfig = argv[1]
    load_f = open(jsonConfig, 'r')
    hjson = json.load(load_f)
    machineId = hjson['MachineId']
    hashFolder = hjson['HashFolder']
    tmpBasePath = hjson['TmpFolder']
    taskUrl = hjson['TaskUrl'] + '/task'
    responseUrl = hjson['TaskUrl'] + '/result'

    if(not os.path.exists(hashFolder) or not os.path.exists(tmpBasePath)):
        print("File path not exist, check your json file.\n")
        return

    threads = []
    for i in range(n_threads):
        t = threading.Thread(target=extractFeatureThread)
        threads.append(t)

    for t in threads:
        t.start()
        
    timer = threading.Timer(10*1, mergeHashTable)
    timer.start()

    for t in threads:
        t.join()

if __name__ == '__main__':
    main(sys.argv)
