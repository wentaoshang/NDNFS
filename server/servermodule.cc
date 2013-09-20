/*
 * Copyright (c) 2013 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Zhe Wen <wenzhe@cs.ucla.edu>
 */
#define NDNFS_DEBUG

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include <boost/lexical_cast.hpp>

#include "servermodule.h"
#include <ndn.cxx/wrapper/wrapper.h>
#include <ndn.cxx/common.h>

using namespace std;
using namespace ndn;
using namespace boost;

extern Ptr<Wrapper> handler;

// callbalck on receiving incoming interest.
// respond proper content object and comsumes the interest. or simple ignore
// the interest if no content object found.
void OnInterest(Ptr<Interest> interest) {
    static int interest_cnt = 0;
#ifdef NDNFS_DEBUG
    cout << interest_cnt++ << "------------------------------------------" << endl;
    cout << "OnInterest(): interest name: " << interest->getName() << endl;
#endif
    string path;
    uint64_t version;
    int seg;
    int res = ProcessName(interest, version, seg, path);
#ifdef NDNFS_DEBUG
    cout << "OnInterest(): extracted version=" << (int64_t)version << ", segment=" << seg << ", path=" << path << endl;
#endif
    if (res == -1) {
		cout << "OnInterest(): no match found for prefix: " << interest->getName() << endl;
    }
    else if(res == 0) {
        cout << "OnInterest(): find match: " << interest->getName() << endl;
    }
    else {
        cout << "OnInterest(): a match has been found for prefix: " << interest->getName() << endl;
        cout << "OnInterest(): fetching content object from database" << endl;

        int len = -1;
        const char* data = NULL;
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT * FROM file_segments WHERE path = ? AND version = ? AND segment = ?", -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, version);
        sqlite3_bind_int(stmt, 3, seg);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            const char * data = (const char *)sqlite3_column_blob(stmt, 3);
            len = sqlite3_column_bytes(stmt, 3);
#ifdef NDNFS_DEBUG
            cout << "OnInterest(): blob length=" << len << endl;
            cout << "OnInterest(): blob data is " << endl;
            ofstream ofs("/tmp/blob", ios_base::binary);
            for (int i = 0; i < len; i++) {
                printf("%02x", (unsigned char)data[i]);
                ofs << data[i];
            }
            cout << endl;
#endif
            ndn::Blob bin_data(data, len);
            handler->putToCcnd(bin_data);
            cout << "OnInterest(): content object returned and interest consumed" << endl;
        }
        else {
            // query failed, no entry found
            cerr << "OnInterest(): error locating data: " << path << endl;
        }
        sqlite3_finalize(stmt);
	}
    cout << "OnInterest(): Done" << endl;
    cout << "------------------------------------------------------------" << endl;
}

// ndn-ndnfs name converter. converting name from ndn::Name representation to
// string representation.
void ndnName2String(const ndn::Name& name, uint64_t &version, int &seg, string &path) {
    path = "";
    string slash("/");
    version = -1;
    seg = -1;
    ndn::Name::const_iterator iter = name.begin();
    for (; iter != name.end(); iter++) {
#ifdef NDNFS_DEBUG
        cout << "ndnName2String(): interest name component: " << iter->toUri() << endl;
#endif
		const uint8_t marker = *(iter->buf());
		// cout << (unsigned int)marker << endl;
		if (marker == 0xFD) {
			version = iter->toVersion(); 
		} 
		else if (marker == 0x00) {
			seg = iter->toSeqNum();
		}
		else {
			string comp = iter->toUri();
			path += (slash + comp);
		}
	
    }
#ifdef NDNFS_DEBUG
    cout << "ndnName2String(): interest name: " << path << endl;
#endif
    path = path.substr(global_prefix.length());
    if (path == "")
		path = string("/");
#ifdef NDNFS_DEBUG
    cout << "ndnName2String(): file path after trimming: " << path << endl;
#endif
}


int ProcessName(Ptr<Interest> interest, uint64_t &version, int &seg, string &path){
    ndnName2String(interest->getName(), version, seg, path);
#ifdef NDNFS_DEBUG
    cout << "ProcessName(): version=" << (int64_t)version << ", segment=" << seg << ", path=" << path << endl;
#endif
    if(version != -1 && seg != -1){
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT * FROM file_segments WHERE path = ? AND version = ? AND segment = ?", -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, version);
        sqlite3_bind_int(stmt, 3, seg);
        if(sqlite3_step(stmt)!= SQLITE_ROW){
#ifdef NDNFS_DEBUG
            cout << "ProcessName(): no such file/directory found in ndnfs: " << path << endl;
#endif
            sqlite3_finalize(stmt);
            return -1;
        }
        sqlite3_finalize(stmt);
        return 1;
    }
    else if(version != -1){
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT * FROM file_versions WHERE path = ? AND version = ? ", -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, version);
        if(sqlite3_step(stmt)!= SQLITE_ROW){
#ifdef NDNFS_DEBUG
            cout << "ProcessName(): no such file/directory found in ndnfs: " << path << endl;
#endif
            sqlite3_finalize(stmt);
            return -1;
        }
		
        SendFile(interest, version, sqlite3_column_int(stmt,2), sqlite3_column_int(stmt,3), 0);
        sqlite3_finalize(stmt);
        return 0;
    }
    else{
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT * FROM file_system WHERE path = ?", -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
        if(sqlite3_step(stmt)!= SQLITE_ROW){
#ifdef NDNFS_DEBUG
            cout << "ProcessName(): no such file/directory found in ndnfs: " << path << endl;
#endif
            sqlite3_finalize(stmt);
            return -1;
        }
        //recursively finding path
        int type = sqlite3_column_int(stmt,2);
        if(type == 1){
#ifdef NDNFS_DEBUG
            cout << "ProcessName(): find file: " << path << endl;
#endif
            version = sqlite3_column_int64(stmt, 7);
            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db, "SELECT * FROM file_versions WHERE path = ? AND version = ? ", -1, &stmt, 0);
            sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 2, version);
            if(sqlite3_step(stmt)!= SQLITE_ROW){
#ifdef NDNFS_DEBUG
                cout << "ProcessName(): no such file/directory found in ndnfs: " << path << endl;
#endif
                sqlite3_finalize(stmt);
                return -1;
            }
	    
            SendFile(interest, version, sqlite3_column_int(stmt,2), sqlite3_column_int(stmt,3),1);
            sqlite3_finalize(stmt);
            return 0;
        }
        int mtime = sqlite3_column_int(stmt, 5);
        sqlite3_finalize(stmt);
#ifdef NDNFS_DEBUG
        cout << "ProcessName(): send dir: " << path << endl;
#endif
        SendDir(interest, path, mtime);
        return 0;
    }
}

/*bool CompareComponent(const string& a, const string& b){
  ndn::Name path1(a);
  ndn::Name path2(b);
  int len1 = path1.size();
  int len2 = path2.size();
  ndn::name::Component& comp1 = path1.get(len1 - 1);
  ndn::name::Component& comp2 = path2.get(len2 - 1);
  return comp1<comp2;
  }*/
void SendFile(Ptr<Interest>interest, uint64_t version, int sizef, int totalseg, int type){
    ndnfs::FileInfo infof;
    infof.set_size(sizef);
    infof.set_totalseg(totalseg);
    infof.set_version(version);
    int size = infof.ByteSize();
    char *wireData = new char[size];
    infof.SerializeToArray(wireData, size);
    Name name = interest->getName();
    if(type == 0)
        name.append("%C1.FS.file");
    else
        name.appendVersion(version).append("%C1.FS.file");
    Content co(wireData, size);
    Data data0;
    data0.setName(name);
    data0.setContent(co);
    keychain->sign(data0, signer);
    Ptr<Blob> send_data = data0.encodeToWire();
    handler->putToCcnd(*send_data);
    return;
}

void SendDir(Ptr<Interest> interest, const string& path, int mtime){
    //finding the relevant file recursively
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM file_system WHERE parent = ?", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
    
    ndnfs::DirInfoArray infoa;
    int count = 0;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        ndnfs::DirInfo *info = infoa.add_di();
        info->set_type(sqlite3_column_int(stmt, 2));
        info->set_path((const char *)sqlite3_column_text(stmt, 0));
        //delete info;
        count++;
        //paths.push_back(string((const char *)sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    //return packet
    if(count!=0){
        int size = infoa.ByteSize();
        char *wireData = new char[size];
        infoa.SerializeToArray(wireData, size);
        Name name = interest->getName();
        name.appendVersion(mtime).append("%C1.FS.dir");
        Content co(wireData, size);
        Data data0;
        data0.setName(name);
        data0.setContent(co);
        keychain->sign(data0, signer);
        Ptr<Blob> send_data = data0.encodeToWire();
        handler->putToCcnd(*send_data);
        delete wireData;
        return;
    }
    else {
#ifdef NDNFS_DEBUG
        cout << "MatchFile(): no such file found in path: " << path << endl;
#endif
    }
    return;
}

/*
  bool CheckSuffix(Ptr<Interest> interest, string path) {
  #ifdef NDNFS_DEBUG
  cout << "CheckSuffix(): checking min/maxSuffixComponents" << endl;
  #endif
  // min/max suffix components
  uint32_t min_suffix_components = interest->getMinSuffixComponents();
  uint32_t max_suffix_components = interest->getMaxSuffixComponents();
  #ifdef NDNFS_DEBUG
  cout << "CheckSuffix(): MinSuffixComponents set to: " << min_suffix_components << endl;
  cout << "CheckSuffix(): MaxSuffixComponents set to: " << max_suffix_components << endl;
  #endif

  // do suffix components check
  uint32_t prefix_len = interest->getName().size();
  string match = global_prefix + path;
  uint32_t match_len = ndn::Name(match).size() + 2;
  // digest considered one component implicitly
  uint32_t suffix_len = match_len - prefix_len + 1;
  if (max_suffix_components != ndn::Interest::ncomps &&
  suffix_len > max_suffix_components) {
  #ifdef NDNFS_DEBUG
  cout << "CheckSuffix(): max suffix mismatch" << endl;
  #endif
  return false;
  }
  if (min_suffix_components != ndn::Interest::ncomps &&
  suffix_len < min_suffix_components) {
  #ifdef NDNFS_DEBUG
  cout << "CheckSuffix(): min suffix mismatch" << endl;
  #endif
  return false;
  }

  return true;
  }
*/

// TODO: publisherPublicKeyDigest
// related implementation not available currently in lib ccnx-cpp
// TODO: exclude
// related implementation not available currently in lib ccnx-cpp


