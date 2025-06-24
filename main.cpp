#include <iostream>
#include <fstream>
#include <vector>
#include <bzlib.h>
#include <sstream>
#include <sys/stat.h>
#include "Bz2Liner.h"

using namespace std;


void decompressChunk(FILE *file, long long start, int len) {
    static double max  = 1;
    fseek(file, start, SEEK_SET);
    char *buf = new char[len];
    fread(buf, 1,len, file);

    unsigned int decrypted_len = len * 25;
    char *bufD;
    int decrypted_ret;
    while (true) {
        bufD = new char[decrypted_len];
        decrypted_ret = BZ2_bzBuffToBuffDecompress(bufD,
                                                       &decrypted_len,
                                                       buf,
                                                       len,
                                                       0,
                                                       0);
        if (decrypted_ret == BZ_OUTBUFF_FULL)
            delete[] bufD;
        else
            break;
        decrypted_len *= 2;
    }
    max = std::max(max, (double)decrypted_len/len);
    // printf("%f\n",max);
    //printf("%d %f %f ret=%d dlen =%d\n",len,(double)decrypted_len/len, max, decrypted_ret, decrypted_len);
    bufD[decrypted_len] = 0;
    //    printf("len = %f MB, decomp %f MB \n",len/1e6, decrypted_len/1e6);
    printf("%s", bufD);
    delete[] bufD;
    delete[] buf;
}

string trimLeft(const string& str)
{
    const auto strBegin = str.find_first_not_of(" \t");
    if (strBegin == string::npos)
        return ""; // no content
    return str.substr(strBegin, str.length() - strBegin);
}


string trimRight(const string& str)
{
    const auto strEnd = str.find_last_not_of(" \t\r");
    if (strEnd == string::npos)
        return ""; // no content
    return str.substr(0, strEnd + 1);
}

string trim(const string& str) {
    return trimLeft(trimRight(str));
}

std::string readDate() {
    std::ifstream file("../dump/DATE");
    std::string date;

    if (file) {
        std::getline(file, date);
    } else
        throw std::runtime_error("run dump.run.sh");

    return trim(date);
}

int main() {
    std::string date = readDate();
    std::string IndexFile = "../dump/enwiktionary-"+ date+ "-pages-articles-multistream-index.txt.bz2";
    std::string WiktFile = "../dump/enwiktionary-"+ date+ "-pages-articles-multistream.xml.bz2";
    FILE *file = fopen(IndexFile.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << IndexFile << "\n";
        return 1;
    }

    BZFILE *bzfile = BZ2_bzReadOpen(nullptr, file, 0, 0, nullptr, 0);
    if (!bzfile) {
        std::cerr << "Could not open bz2 file\n";
        return 1;
    }

    vector<long long int> index;
    const int BufLen = 512 * 1024;
    Bz2Liner sl(BufLen, bzfile);
    long long pn = 0;
    for (std::string line; sl.getline(line);) {
        long long n = stoll(line);
        if (n != pn) {
            index.push_back(n);
            if (index.size() % 10000 == 0)
                cout << "." << flush;
            pn = n;
        }
    }
    struct stat statBuf{};
    stat(WiktFile.c_str(), &statBuf);

    cout << statBuf.st_size << endl;
    index.push_back(statBuf.st_size);
    int bzerror;
    BZ2_bzReadClose(&bzerror, bzfile);
    fclose(file);

    file = fopen(WiktFile.c_str(), "rb");
    if (!file) {
        std::cerr << "Could not open file " << WiktFile << "\n";
        return 1;
    }
   // for (int i=0; i<index.size()-1; i++)
    int i = index.size() - 2;
        decompressChunk(file, index[i], index[i+1]-index[i]);
    fclose(file);
}
