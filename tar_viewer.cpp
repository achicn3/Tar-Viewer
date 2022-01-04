#include <iostream>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <bitset>
#include <experimental/filesystem>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <iomanip>
using namespace std;
namespace fs = std::experimental::filesystem;
#define BLOCK_SIZE 512
#define REGTYPE  '0'            /* regular file */
#define AREGTYPE '\0'           /* regular file */
#define LNKTYPE  '1'            /* hard link */
#define SYMTYPE  '2'            /* symbolic link */
#define CHRTYPE  '3'            /* character special */
#define BLKTYPE  '4'            /* block special */
#define DIRTYPE  '5'            /* directory */
#define FIFOTYPE '6'            /* FIFO  */
#define CONTTYPE '7'            /* Contiguous file */
typedef struct TarHeader {
    char filename[100];
    char filemode[8];
    char userid[8];
    char groupid[8];
    char filesize[12];
    char mtime[12];
    char checksum[8];
    char type;
    char lname[100];
    /* USTAR Section */
    char USTAR_id[6];
    char USTAR_ver[2];
    char username[32];
    char groupname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TarHeader;

vector<unsigned long long int> file_sizes;
vector<string> file_names;
vector<string> directories;
void printErrMsg(const string &msg) { cerr << "[Error] " << msg << endl; }

string determinePermissions(const fs::perms &p,const fs::path& path,fs::file_status s) {
    string permission;
    if(fs::is_directory(s)) permission += "d";
    else if(fs::is_block_file(s)) permission += "b";
    else if(fs::is_character_file(s)) permission += "c";
    else if(fs::is_symlink(s)) permission += "l";
    else permission += "-";
    permission += ((p & fs::perms::owner_read) != fs::perms::none ? "r" : "-");
    permission += ((p & fs::perms::owner_write) != fs::perms::none ? "w" : "-");
    permission += ((p & fs::perms::owner_exec) != fs::perms::none ? "x" : "-");
    permission += ((p & fs::perms::group_read) != fs::perms::none ? "r" : "-");
    permission += ((p & fs::perms::group_write) != fs::perms::none ? "w" : "-");
    permission += ((p & fs::perms::group_exec) != fs::perms::none ? "x" : "-");
    permission += ((p & fs::perms::others_read) != fs::perms::none ? "r" : "-");
    permission += ((p & fs::perms::others_write) != fs::perms::none ? "w" : "-");
    permission += ((p & fs::perms::others_exec) != fs::perms::none ? "x" : "-");
    return permission;
}


string getTime(const fs::path &path) {
    auto ftime = fs::last_write_time(path);
    char buf[64];
    time_t cftime = decltype(ftime)::clock::to_time_t(ftime);
    //Convert last modified time to YYYY-MM-dd HH:mm
    strftime(buf, sizeof buf, "%F %H:%M", localtime(&cftime));
    return string(buf);
}

string getGroupOwnerName(const string &fileName) {
    struct stat info{};
    stat(fileName.c_str(), &info);  // Error check omitted
    struct passwd *pw = getpwuid(info.st_uid);
    struct group *gr = getgrgid(info.st_gid);
    string owner;
    if (gr != nullptr)
        owner += string(gr->gr_name) + "/";
    if (pw != nullptr)
        owner += string(pw->pw_name);
    return owner;
}

bool parseTarFile(const char fileName[]) {
    ifstream fs(fileName);
    if (!fs) {
        printErrMsg("Open file failed.");
        return false;
    }
    char *buffer = new char[BLOCK_SIZE];
    TarHeader *tarHeader = (TarHeader *) buffer;
    fs.seekg(0, fs.end);
    //File length
    int length = fs.tellg();
    fs.seekg(0, fs.beg);
    //Tar file should be multiples of 512
    if (length % BLOCK_SIZE != 0) {
        printErrMsg("Invalid tar file!");
        return false;
    }
    size_t position = 0;
    vector<std::size_t> file_start_addr;
    int dir_count = 0;
    while (true) {
        //Read finish
        if (fs.peek() == EOF){
            break;
        }
        fs.read(buffer, BLOCK_SIZE);
        //Not USTAR
        if (strncmp(tarHeader->USTAR_id, "ustar",5)){
            break;
        }
        //The content of data
        position += BLOCK_SIZE;
        size_t sz;
        auto fileSize = strtoull(tarHeader->filesize,NULL,0);
        //Tar file blocks
        size_t block_count = (fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
        switch (tarHeader->type) {
            case '0': // intentionally dropping through
            case '\0'://Normal file
            case '1': // hard link
            case '2': // symbolic link
            case '3':// device file/special file
            case '4':// block device
                file_names.emplace_back(tarHeader->filename);
                break;
            case '5': // directory
                directories.emplace_back(tarHeader->filename);
                ++dir_count;
                for(auto it : fs::recursive_directory_iterator(tarHeader->filename)){
			        struct stat path_stat;
				    //Directory in directory
				    stat(it.path().c_str(),&path_stat);
				    if(!S_ISDIR(path_stat.st_mode))
					    fileSize += fs::file_size(it.path());
				   // else ++dir_count;
				}
                //Tar file blocks
                block_count += (fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
                break;
            case '6':// named pipe
		        file_names.emplace_back(tarHeader->filename);
                break;
            default:
                break;
        }

        position += block_count * (BLOCK_SIZE+1);
        //Next block
        fs.seekg(position, ios::beg);
    }

    //Set cursor of file to the head
    fs.seekg(0, ios::cur);
    return true;
}

void outputEntry(const string& fileName,int& idx){
    string permission = determinePermissions(fs::status(fileName).permissions(),fs::path(fileName),fs::status(fileName));
    cout.setf(ios::fixed);
    cout << permission <<" "<< getGroupOwnerName(fileName)
         <<setw(8)<< (permission[0] == 'd' ? 0 : fs::file_size(fileName)) <<" "<< getTime(fs::path(fileName))<<" "<<fileName<<endl;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printErrMsg("Invalid usage. Usage: "+ string(argv[0])+" [your_file].tar");
        return -1;
    }
    if (parseTarFile(argv[1])) {
        int idx = 0;
        for (auto &fileName : file_names)
            outputEntry(fileName,idx);
        for( auto &dirName :directories){
            for(auto it : fs::recursive_directory_iterator(dirName)){
                outputEntry(string(it.path()),idx);
            }
        }
    }
}

