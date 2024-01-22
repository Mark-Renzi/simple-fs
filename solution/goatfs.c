#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "disk.h"
#include "goatfs.h"

bool mounted = false;
char * bmp = 0; // 1 is free, 2 is full, 0 is sentinel

unsigned int min(unsigned int a, unsigned int b) {
    return a<b ? a : b;
}

void debug(){
	//printf("%d file descriptor\n", _disk->FileDescriptor);
	
	Block block;
	
	memset( block.Data, '\0', sizeof(char)*BLOCK_SIZE );
	wread(0, block.Data);
	
	printf("SuperBlock:\n");
	if (block.Super.MagicNumber == MAGIC_NUMBER){
		printf("    magic number is valid\n");
	} else {
		printf("    magic number is invalid\n");
		exit(ERR_BAD_MAGIC_NUMBER);
	}
	
	printf("    %u blocks\n", block.Super.Blocks);
	printf("    %u inode blocks\n", block.Super.InodeBlocks);
	printf("    %u inodes\n", block.Super.Inodes);
	
	for (unsigned int i = 0; i < block.Super.InodeBlocks; i++){ //for each inode BLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inodeBlock.Inodes[j].Valid == 1){
				printf("Inode %u:\n", i*INODES_PER_BLOCK+j);
				printf("    size: %u bytes\n", inodeBlock.Inodes[j].Size);
				unsigned int count = 0;
				printf("    direct blocks:");
				for (unsigned int k = 0; k < POINTERS_PER_INODE; k ++){//5 possible directs
					//printf("    [DEBUG] datablock: %u\n", inodeBlock.Inodes[j].Direct[k]);
					if (inodeBlock.Inodes[j].Direct[k] != 0){
						count++;
						printf(" %u", inodeBlock.Inodes[j].Direct[k]);
					}
				}
				printf("\n");
				
				if (count >= 5) { //INDIRECT BLOCKS
					Block pointerBlock;
					memset( pointerBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
					wread(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
					printf("    indirect block: %u\n", inodeBlock.Inodes[j].Indirect);
					
					//unsigned int indirectCount = 0;
					printf("    indirect data blocks:");
					for (unsigned int k = 0; k < POINTERS_PER_BLOCK; k++) {
						if (pointerBlock.Pointers[k] != 0){
							//indirectCount++;
							printf(" %u", pointerBlock.Pointers[k]);
							
						}
					}
					printf("\n");
				}
			}
		}
	}
}


bool format(){
	Block block;
	
	memset( block.Data, '\0', sizeof(char)*BLOCK_SIZE );
	//wread(0, block.Data);
	
	unsigned int totalBlocks = _disk->Blocks;
	unsigned int inodeBlockNum = ceil(totalBlocks*0.1);
	if (totalBlocks < 1 || inodeBlockNum >= totalBlocks || inodeBlockNum < 1 || mounted) {
		return false; //could not format with these conditions
	}
	//printf("%u", inodeBlockNum); 1 on 5.tmp

	block.Super.MagicNumber = MAGIC_NUMBER;
	block.Super.Blocks = totalBlocks;
	block.Super.InodeBlocks = inodeBlockNum;
	block.Super.Inodes = inodeBlockNum*INODES_PER_BLOCK;
	
	wwrite(0, block.Data);
	unsigned int i = 0;
	for (; i < block.Super.InodeBlocks; i++){ //for each inode BLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			inodeBlock.Inodes[j].Valid = 0;
		}
		
		wwrite(i+1, inodeBlock.Data); //+1 to skip superblock
	}
	
	i++; //account for superblock
	{
		Block blankBlock;
		memset( blankBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		for (; i < totalBlocks; i++){
			wwrite(i, blankBlock.Data);
		}
	}
	
	mounted = false;
	free(bmp);
	
    return true;
}



int mount(){
	
	if (mounted) {
		return -1;
	}
	
	Block block;
	
	memset( block.Data, '\0', sizeof(char)*BLOCK_SIZE );
	wread(0, block.Data);
	
	if (block.Super.Blocks < 1 || block.Super.InodeBlocks >= block.Super.Blocks || block.Super.InodeBlocks < 1 || block.Super.Blocks > _disk->Blocks || block.Super.MagicNumber != 0xf0f03410 || block.Super.Inodes < 1 || block.Super.InodeBlocks * INODES_PER_BLOCK != block.Super.Inodes) {
		return -1; //could not mount with these conditions
	}
	
	bmp = malloc( sizeof(char) * (block.Super.Inodes + 1));
	memset( bmp, '\0', sizeof(char) * (block.Super.Inodes + 1) );
	memset( bmp, 1, sizeof(char) * (block.Super.Inodes) );
	
	bmp[0] = 2;
	
	for (unsigned int i = 0; i < block.Super.InodeBlocks; i++){ //for each inode BLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		bmp[i+1] = 2;
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inodeBlock.Inodes[j].Valid == 1){

				unsigned int count = 0;
				for (unsigned int k = 0; k < POINTERS_PER_INODE; k ++){//5 possible directs
					//printf("    [DEBUG] datablock: %u\n", inodeBlock.Inodes[j].Direct[k]);
					if (inodeBlock.Inodes[j].Direct[k] != 0){
						count++;
						bmp[inodeBlock.Inodes[j].Direct[k]] = 2;
					}
				}
				
				if (count >= 5) { //INDIRECT BLOCKS
					Block pointerBlock;
					memset( pointerBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
					wread(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
					bmp[inodeBlock.Inodes[j].Indirect] = 2;
					
					for (unsigned int k = 0; k < POINTERS_PER_BLOCK; k++) {
						if (pointerBlock.Pointers[k] != 0){
							bmp[pointerBlock.Pointers[k]] = 2;
						}
					}
				}
			}
		}
	}
	
	mounted = true;
	
    return SUCCESS_GOOD_MOUNT;
}

ssize_t create(){

	if (!mounted) {
		return -1;
	}

	//Block block;
	
	//memset( block.Data, '\0', sizeof(char)*BLOCK_SIZE );
	//wread(0, block.Data);
	
	unsigned int len = 0;
	while (bmp[len] != '\0') {
		len++;
	}
	
	for (unsigned int i = 0; i < len/INODES_PER_BLOCK; i++){ //foreach inodeBLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inodeBlock.Inodes[j].Valid != 1){
				inodeBlock.Inodes[j].Valid = 1;
				inodeBlock.Inodes[j].Size = 0;
				for (unsigned int k = 0; k < POINTERS_PER_INODE; k++){
					inodeBlock.Inodes[j].Direct[k] = 0;
				}
				inodeBlock.Inodes[j].Indirect = 0;
				
				wwrite(i+1, inodeBlock.Data); //+1 to skip superblock
				
				return i*INODES_PER_BLOCK+j;
			}
		}
	}
	
    return -1;
}

bool wremove(size_t inumber){
	if (!mounted) {
		return -1;
	}

	//Block block;
	
	//memset( block.Data, '\0', sizeof(char)*BLOCK_SIZE );
	//wread(0, block.Data);
	
	unsigned int len = 0;
	while (bmp[len] != '\0') {
		len++;
	}
	
	for (unsigned int i = 0; i < len/INODES_PER_BLOCK; i++){ //foreach inodeBLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inumber == i*INODES_PER_BLOCK+j){
				if (inodeBlock.Inodes[j].Valid == 1) {
					inodeBlock.Inodes[j].Valid = 0;
					for (unsigned int k = 0; k < POINTERS_PER_INODE; k++){
						if (inodeBlock.Inodes[j].Direct[k] != 0) {
							bmp[inodeBlock.Inodes[j].Direct[k]] = 1;
							inodeBlock.Inodes[j].Direct[k] = 0;
						}
					}
					
					if (inodeBlock.Inodes[j].Indirect != 0){
						Block pointerBlock;
						memset( pointerBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
						wread(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
						
						for (unsigned int k = 0; k < POINTERS_PER_BLOCK; k++) {
							if (pointerBlock.Pointers[k] != 0){
								bmp[pointerBlock.Pointers[k]] = 1;
								pointerBlock.Pointers[k] = 0;
							}
						}
						bmp[inodeBlock.Inodes[j].Indirect] = 1;
						inodeBlock.Inodes[j].Indirect = 0;
						
						wwrite(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
					}
					
					wwrite(i+1, inodeBlock.Data); //+1 to skip superblock
					
					return true;
				} else {
					return false;
				}
			}
		}
	}
	
    return false;
}
ssize_t stat(size_t inumber) {
	if (!mounted) {
		return -1;
	}
	
	unsigned int len = 0;
	while (bmp[len] != '\0') {
		len++;
	}
	
	for (unsigned int i = 0; i < len/INODES_PER_BLOCK; i++){ //foreach inodeBLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inumber == i*INODES_PER_BLOCK+j){
				if (inodeBlock.Inodes[j].Valid == 1) {
					return inodeBlock.Inodes[j].Size;
				} else {
					return -1;
				}
			}
		}
	}
	
    return -1;
}
ssize_t wfsread(size_t inumber, char* data, size_t length, size_t offset){
	if (!mounted) {
		return -1;
	}
	
	
	
	
	unsigned int len = 0;
	while (bmp[len] != '\0') {
		len++;
	}
	
	//printf("OFFSET: %lu\n", offset);
	
	size_t progress = 0;
	
	for (unsigned int i = 0; i < len/INODES_PER_BLOCK; i++){ //for each inode BLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inumber == i*INODES_PER_BLOCK+j){
				if (inodeBlock.Inodes[j].Valid == 1) {
					if (offset >= inodeBlock.Inodes[j].Size) {
						return 0;
					}
					for (unsigned int k = 0; k < POINTERS_PER_INODE; k ++){//5 possible directs
						if (inodeBlock.Inodes[j].Direct[k] != 0){
							if ((progress < inodeBlock.Inodes[j].Size) && (progress < offset+length)) {
								if (progress < offset) {
									progress += 4096;
								} else {
									wread(inodeBlock.Inodes[j].Direct[k], &(data[progress-offset]));
									//printf("%lu-", progress);
									progress += min(min(4096, inodeBlock.Inodes[j].Size-progress), offset+length-progress);
									if (progress >= inodeBlock.Inodes[j].Size || progress >= offset+length) {
										//printf("%lu/%lu, %u\n", progress-offset, length, inodeBlock.Inodes[j].Size);
										return progress-offset;
									}
								}
							}

						}
					}

					if (inodeBlock.Inodes[j].Indirect != 0) { //INDIRECT BLOCKS
						Block pointerBlock;
						memset( pointerBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
						wread(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
						
						for (unsigned int k = 0; k < POINTERS_PER_BLOCK; k++) {
							if (pointerBlock.Pointers[k] != 0){
								//printf("%lu/%u %lu/%lu\n", offset, inodeBlock.Inodes[j].Size, progress, length);
								if ((progress < inodeBlock.Inodes[j].Size) && (progress < offset+length)) {
									if (progress < offset) {
										progress += 4096;
									} else {
										wread(pointerBlock.Pointers[k], &(data[progress-offset]));
										//printf("%lu+", progress);
										progress += min(min(4096, inodeBlock.Inodes[j].Size-progress), offset+length-progress);
										if (progress >= inodeBlock.Inodes[j].Size || progress >= offset+length) {
											//printf("%lu/%lu, %u\n", progress-offset, length, inodeBlock.Inodes[j].Size);
											return progress-offset;
										}
									}
								}
							}
						}
					}
				
				} else {
					return -1;
				}
			}
		}
	}
    return -1;
}
ssize_t wfswrite(size_t inumber, char* data, size_t length, size_t offset){
		if (!mounted) {
		return -1;
	}

	
	unsigned int len = 0;
	unsigned int space = 0;
	while (bmp[len] != '\0') {
		//printf("%d", bmp[len]);
		if (bmp[len] == 1){ space++; }
		len++;
	}
	//printf("\n");
	
	//printf("----------%u/%lu\n", space, length/4096);
	//if (space < length/4096) { //NOT ENOUGH SPACE
	//	return -1;
	//}
	
	
	size_t progress = 0;
	for (unsigned int i = 0; i < len/INODES_PER_BLOCK; i++){ //for each inode BLOCK (i)
		Block inodeBlock;
		memset( inodeBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
		wread(i+1, inodeBlock.Data); //+1 to skip superblock
		
		for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) { //for each inode (j)
			if (inumber == i*INODES_PER_BLOCK+j){
				if (inodeBlock.Inodes[j].Valid == 1) {
					for (unsigned int k = 0; k < POINTERS_PER_INODE; k ++){//5 possible directs
						if (progress < offset+length) {
							if (progress < offset) {
								progress += 4096;
							} else {
								if (space > 0) {
									space--;
									unsigned int f = 0; //f is next free block
									while (bmp[f] != 1) {
										f++;
									}
									if (f < _disk->Blocks) {
										bmp[f] = 2;
										inodeBlock.Inodes[j].Direct[k] = f;
										inodeBlock.Inodes[j].Size += min(4096, offset+length-progress);
										wwrite(f, &(data[progress-offset]));
										//printf("%lu-", progress);
										progress += min(4096, offset+length-progress);
										if (progress >= offset+length) {
											wwrite(i+1, inodeBlock.Data);
											return progress-offset;
										}
									} else {
										//printf("OUT OF SPACE\n");
										wwrite(i+1, inodeBlock.Data);
										return progress-offset;
									}
								} else { // out of space
									wwrite(i+1, inodeBlock.Data);
									return progress-offset;
								}
							}
						}
					}

					Block pointerBlock;
					memset( pointerBlock.Data, '\0', sizeof(char)*BLOCK_SIZE );
					
					if (inodeBlock.Inodes[j].Indirect == 0) { //make a pointer block
						if (space > 0) {
							space--;
							unsigned int f = 0; //f is next free block
							while (bmp[f] != 1) {
								f++;
							}
							if (f < _disk->Blocks) {
								bmp[f] = 2;
								inodeBlock.Inodes[j].Indirect = f;
								wwrite(i+1, inodeBlock.Data);
								wwrite(f, pointerBlock.Data);
							} else {
								//printf("OUT OF SPACE\n");
								wwrite(i+1, inodeBlock.Data);
								return progress-offset;
							}
						} else { // out of space
							//printf("OUT OF SPACE\n");
							wwrite(i+1, inodeBlock.Data);
							return progress-offset;
						}
					} else {
						wread(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
					}
					
					
					for (unsigned int k = 0; k < POINTERS_PER_BLOCK; k++) {
						if (progress < offset+length) {
							if (progress < offset) {
								progress += 4096;
							} else {
								if (space > 0) {
									space--;
									unsigned int f = 0; //f is next free block
									while (bmp[f] != 1) {
										f++;
									}
									if (f >= _disk->Blocks){
										//printf("OUT OF SPACE\n");
										wwrite(i+1, inodeBlock.Data);
										return progress-offset;
									}
									

									bmp[f] = 2;
									pointerBlock.Pointers[k] = f;
									wwrite(inodeBlock.Inodes[j].Indirect, pointerBlock.Data);
									inodeBlock.Inodes[j].Size += min(4096, offset+length-progress);
									wwrite(f, &(data[progress-offset]));
									//printf("%lu-", progress);
									progress += min(4096, offset+length-progress);
									if (progress >= offset+length) {
										wwrite(i+1, inodeBlock.Data);
										return progress-offset;
									}
								} else { // out of space
									//printf("OUT OF SPACE\n");
									wwrite(i+1, inodeBlock.Data);
									return progress-offset;
								}
							}
						}
					}
			
				} else {
					return -1;
				}
			}
		}
	}
    return -1;
}
