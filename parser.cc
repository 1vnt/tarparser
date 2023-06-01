/*
 * Copyright (c) 2023 Vincent Schweiger<github.com@xolley.de>.
 * All Rights Reserved.
 */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#define ROUND_UP(addr, align) (((addr) + align - 1) & ~(align - 1))

using std::cout;
using std::endl;

enum [[gnu::packed]] FileType { Reg = '0', Hrd, Sym, Chr, Blk, Dir };

struct [[gnu::packed]] TarHeader {
  char filename[100];
  // following are octal:
  char mode[8];
  char uid[8];
  char gid[8];
  char filesize[12];
  char mtime[12];
  // not octal
  uint64_t chksum;
  FileType filetype;
  char linkname[100];
  char magic[6]; // ustar then \0
  uint16_t ustar_version;
  char user_name[32];
  char group_name[32];
  uint64_t device_major;
  uint64_t device_minor;
  char filename_prefix[155];

  char _padding[12];
};

static_assert(sizeof(TarHeader) == 512, "Tarheader has the wrong size");

static uint64_t decodeTarOctal(char *data, size_t size) {
  uint8_t *currentPtr = (uint8_t *)data + size;
  uint64_t sum = 0;
  uint64_t currentMultiplier = 1;

  /* find then last NUL or space */
  uint8_t *checkPtr = currentPtr;
  for (; checkPtr >= (uint8_t *)data; checkPtr--) {
    if (*checkPtr == 0 || *checkPtr == ' ')
      currentPtr = checkPtr - 1;
  }
  /* decode the octal number */
  for (; currentPtr >= (uint8_t *)data; currentPtr--) {
    sum += (uint64_t)((*currentPtr) - 48) * currentMultiplier;
    currentMultiplier *= 8;
  }
  return sum;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    cout << "Supply the tar file as first argument" << endl;
    return 1;
  }

  auto tarfile = std::ifstream(argv[1], std::ios::binary);

  auto zeroFilled = 0;

  while (!tarfile.eof()) {
    if (zeroFilled >= 2) {
      break;
    }

    TarHeader header;
    tarfile.read((char *)&header, sizeof(TarHeader));

    if (header.filetype == 0) {
      zeroFilled++;
      continue;
    }

    zeroFilled = 0;

    switch (header.filetype) {

    case FileType::Reg: {
      // Open file with correct permissions
      auto f = open(header.filename,
                    decodeTarOctal(header.mode, sizeof(header.mode)));
      close(f);

      // Reopen it now as a ofstream
      auto out = std::ofstream(header.filename, std::ios::binary);
      auto size = decodeTarOctal(header.filesize, sizeof(header.filesize));

      std::vector<char> buffer(size);
      tarfile.read(buffer.data(), buffer.size());
      out.write(buffer.data(), buffer.size());
      out.close();

      tarfile.ignore(ROUND_UP(size, 512) - size);

      break;
    }

    case FileType::Hrd: {
      link(header.linkname, header.filename);
      break;
    }

    case FileType::Sym: {
      symlink(header.linkname, header.filename);
      break;
    }

    case FileType::Dir: {
      mkdir(header.filename, decodeTarOctal(header.mode, sizeof(header.mode)));
      break;
    }

    default:
      cout << "Unhandled filetype: '" << (char)header.filetype << "'" << endl;
      break;
    }
  }

  tarfile.close();
  return 0;
}
