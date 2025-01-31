﻿#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "filesys.h"

/*
 * 内存中数据存储有两种形式，低位存储（Little Endian），高位存储（Big Endian）。
 * 以下宏的目的是在这两种存储方式之间相互转换
 * 一般来说，硬盘上是高位存储，电脑上是低位存储。但这不是确定的。
 * 参考资料http://www.cnblogs.com/renyuan/archive/2013/05/26/3099766.html
 */
/*  
以0x12345678为例： 
    Big Endian 
    低地址                              高地址 
    -----------------------------------------> 
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
    |   12   |   34  |   56   |   78    | 
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
    Little Endian 
    低地址                              高地址 
    -----------------------------------------> 
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
    |   78   |   56  |   34   |   12    | 
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
*/

#define RevByte(low,high) ((high)<<8|(low))
#define RevWord(lowest,lower,higher,highest) ((highest)<< 24|(higher)<<16|(lower)<<8|lowest) 

int JGmkdir(char filename[])
{
	struct Entry *pentry;
	int ret,i=0,cluster_addr,offset,size=0;
	int year, month, day, hour, min, sec;
	time_t now;
	unsigned short cluster,clusterno[100];
	unsigned char c[DIR_ENTRY_SIZE];
	int index,clustersize;
	unsigned char buf[DIR_ENTRY_SIZE];
	pentry = (struct Entry*)malloc(sizeof(struct Entry));
	struct tm *t;

	clustersize = 1;

	//扫描根目录，是否已存在该文件名
	ret = ScanEntry(filename,pentry,1);
	if (ret<0)
	{
		/*查询fat表，找到空白簇，保存在clusterno[]中*/
		for(cluster=2;cluster<1000;cluster++)
		{
			index = cluster *2;
			if(fatbuf[index]==0x00&&fatbuf[index+1]==0x00)
			{
				clusterno[i] = cluster;

				i++;
				if(i==clustersize)
					break;

			}

		}

		/*在fat表中写入下一簇信息*/
		for(i=0;i<clustersize-1;i++)
		{
			index = clusterno[i]*2;

			fatbuf[index] = (clusterno[i+1] &  0x00ff);
			fatbuf[index+1] = ((clusterno[i+1] & 0xff00)>>8);


		}
		/*最后一簇写入0xffff*/
		index = clusterno[i]*2;
		fatbuf[index] = 0xff;
		fatbuf[index+1] = 0xff;

		if(curdir==NULL)  /*往根目录下写文件*/
		{ 

			if((ret= lseek(fd,ROOTDIR_OFFSET,SEEK_SET))<0)
				perror("lseek ROOTDIR_OFFSET failed");
			offset = ROOTDIR_OFFSET;
			while(offset < DATA_OFFSET)
			{
			  //读取一个条目
				if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
					perror("read entry failed");

				offset += abs(ret);
				//看看条目是否可用（e5）或者是不是表示后面没有更多条目（00）
				if(buf[0]!=0xe5&&buf[0]!=0x00)
				{
				  //buf[11]是attribute，但是感觉下面这个while循环并没有什么卵用。。。
				  while(buf[11] == 0x0f)
					{
						if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
							perror("read root dir failed");
						offset +=abs(ret);
					}
				}


				/*找出空目录项或已删除的目录项*/ 
				else
				{       
					offset = offset-abs(ret);     
					for(i=0;i<=strlen(filename);i++)
					{
						c[i]=toupper(filename[i]);
					}			
					for(;i<=10;i++)
						c[i]=' ';

					c[11] = 0x10;

					time(&now);
					t = localtime(&now);
					year = t->tm_year - 80;
					month = t->tm_mon + 1;
					day = t->tm_mday;
					hour = t->tm_hour;
					min = t->tm_min;
					sec = t->tm_sec;
					/*修改时间*/
					c[22] = (((hour << 11) + (min << 5) + (sec >> 1)) & 0x00ff);
					c[23] = ((((hour << 11) + (min << 5) + (sec >> 1)) & 0xff00) >> 8);

					/*修改日期*/
					c[24] = (((year << 9) + (month << 5) + day) & 0x00ff);
					c[25] = ((((year << 9) + (month << 5) + day) & 0xff00) >> 8);

					/*写第一簇的值*/
					c[26] = (clusterno[0] &  0x00ff);
					c[27] = ((clusterno[0] & 0xff00)>>8);

					/*写文件的大小*/
					c[28] = (size &  0x000000ff);
					c[29] = ((size & 0x0000ff00)>>8);
					c[30] = ((size& 0x00ff0000)>>16);
					c[31] = ((size& 0xff000000)>>24);

					/*而且这里还有个问题，就是对于目录表项的值为00的情况处理的不好*/
					if(lseek(fd,offset,SEEK_SET)<0)
						perror("lseek fd_cf failed");
					if(write(fd,&c,DIR_ENTRY_SIZE)<0)
						perror("write failed");

					free(pentry);
					if(WriteFat()<0)
						exit(1);

					return 1;
				}

			}
		}
		else 
		{
		  //子目录的情况与根目录类似
			cluster_addr = (curdir->FirstCluster -2 )*CLUSTER_SIZE + DATA_OFFSET;
			if((ret= lseek(fd,cluster_addr,SEEK_SET))<0)
				perror("lseek cluster_addr failed");
			offset = cluster_addr;
			while(offset < cluster_addr + CLUSTER_SIZE)
			{
				if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
					perror("read entry failed");

				offset += abs(ret);

				if(buf[0]!=0xe5&&buf[0]!=0x00)
				{
					while(buf[11] == 0x0f)
					{
						if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
							perror("read root dir failed");
						offset +=abs(ret);
					}
				}
				else
				{ 
					offset = offset - abs(ret);      
					for(i=0;i<=strlen(filename);i++)
					{
						c[i]=toupper(filename[i]);
					}
					for(;i<=10;i++)
						c[i]=' ';

					c[11] = 0x10;

					time(&now);
					t = localtime(&now);
					year = t->tm_year - 80;
					month = t->tm_mon + 1;
					day = t->tm_mday;
					hour = t->tm_hour;
					min = t->tm_min;
					sec = t->tm_sec;
					/*修改时间*/
					c[22] = (((hour << 11) + (min << 5) + (sec >> 1)) & 0x00ff);
					c[23] = ((((hour << 11) + (min << 5) + (sec >> 1)) & 0xff00) >> 8);

					/*修改日期*/
					c[24] = (((year << 9) + (month << 5) + day) & 0x00ff);
					c[25] = ((((year << 9) + (month << 5) + day) & 0xff00) >> 8);

					c[26] = (clusterno[0] &  0x00ff);
					c[27] = ((clusterno[0] & 0xff00)>>8);

					c[28] = (size &  0x000000ff);
					c[29] = ((size & 0x0000ff00)>>8);
					c[30] = ((size& 0x00ff0000)>>16);
					c[31] = ((size& 0xff000000)>>24);

					if(lseek(fd,offset,SEEK_SET)<0)
						perror("lseek fd_cf failed");
					if(write(fd,&c,DIR_ENTRY_SIZE)<0)
						perror("write failed");

					free(pentry);
					if(WriteFat()<0)
						exit(1);

					return 1;
				}

			}
		}
	}
	else
	{
		printf("This dirname is exist\n");
		free(pentry);
		return -1;
	}
	return 1;

}

int YHisEmpty(struct Entry *pentry)
{
	int ret,offset,i;
	int cluster_addr;
	struct Entry test;

	/*扫描根目录*/
	if(pentry == NULL)  
	{
		return 0;
	}

	/*扫描子目录*/
	else  
	{
		cluster_addr = DATA_OFFSET + (pentry->FirstCluster -2)*CLUSTER_SIZE;
		if((ret = lseek(fd,cluster_addr,SEEK_SET))<0)
			perror("lseek cluster_addr failed");
		offset= cluster_addr;

		while(offset<cluster_addr + CLUSTER_SIZE)
		{
			ret= GetEntry(&test);
			offset += abs(ret);
			if (ret > 0)
				return 0;
		}
		return 1;
	}
}

int YHrmdir(char filename[], struct Entry *dir)
{
	struct Entry *pentry; 
	struct Entry test;
	int ret, cluster_addr;
	unsigned char c;
	unsigned short seed,next;

	pentry = (struct Entry*)malloc(sizeof(struct Entry));

	/*扫描当前目录查找文件*/
	ret = ScanEntry(filename,pentry,1);
	if(ret<0)
	{
		printf("no such directory\n");
		free(pentry);
		return -1;
	}

	if (YHisEmpty(pentry) == 0)
	{
		printf("directory is not empty\n");
		free(pentry);
		return -1;
	}

	/*清除fat表项*/
	seed = pentry->FirstCluster;
	while((next = GetFatCluster(seed))!=0xffff)
	{
		ClearFatCluster(seed);
		seed = next;

	}

	ClearFatCluster( seed );

	/*清除目录表项*/
	c=0xe5;//e5表示该目录项可用

	//现将文件指针定位到目录处，0x20等价于32，因为每条目录表项32bytes
	if(lseek(fd,ret-0x20,SEEK_SET)<0)
		perror("lseek fd_df failed");
	//标记目录表项可用
	if(write(fd,&c,1)<0)
		perror("write failed");  

	free(pentry);
	if(WriteFat()<0)
		exit(1);
	return 1;
}

/*
 *读取BootSector，获取FAT16的格式信息
 *打印启动项记录
 */
void ScanBootSector()
{	
	unsigned char buf[SECTOR_SIZE];
	int ret,i;
	//将BootSector读入缓冲区
	if((ret = read(fd,buf,SECTOR_SIZE))<0)
		perror("read boot sector failed");
	//获取FAT16信息
	for(i = 0; i < 8; i++)
		bdptor.Oem_name[i] = buf[i+0x03];
	bdptor.Oem_name[i] = '\0';

	bdptor.BytesPerSector = RevByte(buf[0x0b],buf[0x0c]);
	bdptor.SectorsPerCluster = buf[0x0d];
	bdptor.ReservedSectors = RevByte(buf[0x0e],buf[0x0f]);
	bdptor.FATs = buf[0x10];
	bdptor.RootDirEntries = RevByte(buf[0x11],buf[0x12]);    
	bdptor.LogicSectors = RevByte(buf[0x13],buf[0x14]);
	if(bdptor.LogicSectors == 0)
		bdptor.LogicSectors=RevWord(buf[0x20],buf[0x21],buf[0x22],buf[0x23]);
	bdptor.MediaType = buf[0x15];
	bdptor.SectorsPerFAT = RevByte( buf[0x16],buf[0x17] );
	bdptor.SectorsPerTrack = RevByte(buf[0x18],buf[0x19]);
	bdptor.Heads = RevByte(buf[0x1a],buf[0x1b]);
	bdptor.HiddenSectors = RevByte(buf[0x1c],buf[0x1d]);
	
	FAT_TWO_OFFSET = bdptor.BytesPerSector + bdptor.SectorsPerFAT * bdptor.BytesPerSector;
        ROOTDIR_OFFSET = bdptor.BytesPerSector + bdptor.FATs * bdptor.SectorsPerFAT * bdptor.BytesPerSector;
	DATA_OFFSET = bdptor.BytesPerSector + bdptor.FATs * bdptor.SectorsPerFAT * bdptor.BytesPerSector + DIR_ENTRY_SIZE*bdptor.RootDirEntries;

	printf("Oem_name \t\t%s\n"
		"BytesPerSector \t\t%d\n"
		"SectorsPerCluster \t%d\n"
		"ReservedSector \t\t%d\n"
		"FATs \t\t\t%d\n"
		"RootDirEntries \t\t%d\n"
		"LogicSectors \t\t%d\n"
		"MedioType \t\t%d\n"
		"SectorPerFAT \t\t%d\n"
		"SectorPerTrack \t\t%d\n"
		"Heads \t\t\t%d\n"
		"HiddenSectors \t\t%d\n"
		"FAT_ONE_OFFSET \t\t%d\n"
		"FAT_TWO_OFFSET \t\t%d\n"
		"ROOTDIR_OFFSET \t\t%d\n"
		"DATA_OFFSET \t\t%d\n",
		bdptor.Oem_name,
		bdptor.BytesPerSector,
		bdptor.SectorsPerCluster,
		bdptor.ReservedSectors,
		bdptor.FATs,
		bdptor.RootDirEntries,
		bdptor.LogicSectors,
		bdptor.MediaType,
		bdptor.SectorsPerFAT,
		bdptor.SectorsPerTrack,
		bdptor.Heads,
		bdptor.HiddenSectors,
		FAT_ONE_OFFSET,
		FAT_TWO_OFFSET,
		ROOTDIR_OFFSET,
		DATA_OFFSET);
}

/*日期*/
void findDate(unsigned short *year,
			  unsigned short *month,
			  unsigned short *day,
			  unsigned char info[2])
{
	int date;
	date = RevByte(info[0],info[1]);

	*year = ((date & MASK_YEAR)>> 9 )+1980;
	*month = ((date & MASK_MONTH)>> 5);
	*day = (date & MASK_DAY);
}

/*时间*/
void findTime(unsigned short *hour,
			  unsigned short *min,
			  unsigned short *sec,
			  unsigned char info[2])
{
	int time;
	time = RevByte(info[0],info[1]);

	*hour = ((time & MASK_HOUR )>>11);
	*min = (time & MASK_MIN)>> 5;
	*sec = (time & MASK_SEC) * 2;
}

/*
*文件名格式化，便于比较
*/
void FileNameFormat(unsigned char *name)
{
	unsigned char *p = name;
	while(*p!='\0')
		p++;
	p--;
	while(*p==' ')
		p--;
	p++;
	*p = '\0';
}

/*参数：entry，类型：struct Entry*
*返回值：成功，则返回偏移值；失败：返回负值
*功能：从根目录或文件簇中得到文件表项
*/
int GetEntry(struct Entry *pentry)
{
	int ret,i;
	int count = 0;
	unsigned char buf[DIR_ENTRY_SIZE], info[2];

	/*读一个目录表项，即32字节*/
	if( (ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
		perror("read entry failed");
	count += ret;

	if(buf[0]==0xe5 || buf[0]== 0x00)
		return -1*count;
	else
	{
		/*长文件名，忽略掉*/
		while (buf[11]== 0x0f) 
		{
			if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
				perror("read root dir failed");
			count += ret;
		}

		/*命名格式化，主义结尾的'\0'*/
		for (i=0 ;i<=10;i++)
			pentry->short_name[i] = buf[i];
		pentry->short_name[i] = '\0';
		//去掉文件名结尾的空格
		FileNameFormat(pentry->short_name); 


		//以下两个还原文件时间的函数有问题，请参照pdf文档修正
		info[0]=buf[22];
		info[1]=buf[23];
		findTime(&(pentry->hour),&(pentry->min),&(pentry->sec),info);  

		info[0]=buf[24];
		info[1]=buf[25];
		findDate(&(pentry->year),&(pentry->month),&(pentry->day),info);

		pentry->FirstCluster = RevByte(buf[26],buf[27]);
		pentry->size = RevWord(buf[28],buf[29],buf[30],buf[31]);

		pentry->readonly = (buf[11] & ATTR_READONLY) ?1:0;
		pentry->hidden = (buf[11] & ATTR_HIDDEN) ?1:0;
		pentry->system = (buf[11] & ATTR_SYSTEM) ?1:0;
		pentry->vlabel = (buf[11] & ATTR_VLABEL) ?1:0;
		pentry->subdir = (buf[11] & ATTR_SUBDIR) ?1:0;
		pentry->archive = (buf[11] & ATTR_ARCHIVE) ?1:0;

		return count;
	}
}

/*
*功能：显示当前目录的内容
*返回值：1，成功；-1，失败
*/
int fd_ls()
{
	int ret, offset,cluster_addr;
	struct Entry entry;
	unsigned char buf[DIR_ENTRY_SIZE];
	if( (ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
		perror("read entry failed");
	if(curdir==NULL)
		printf("Root_dir\n");
	else
		printf("%s_dir\n",curdir->short_name);
	printf("\tname\tdate\t\t time\t\tcluster\tsize\t\tattr\n");

	if(curdir==NULL)  /*显示根目录区*/
	{
		/*将fd定位到根目录区的起始地址*/
		if((ret= lseek(fd,ROOTDIR_OFFSET,SEEK_SET))<0)
			perror("lseek ROOTDIR_OFFSET failed");

		offset = ROOTDIR_OFFSET;

		/*从根目录区开始遍历，直到数据区起始地址*/
		while(offset < (DATA_OFFSET))
		{
			ret = GetEntry(&entry);

			offset += abs(ret);
			if(ret > 0)
			{
				printf("%12s\t"
					"%d:%d:%d\t"
					"%d:%d:%d   \t"
					"%d\t"
					"%d\t\t"
					"%s\n",
					entry.short_name,
					entry.year,entry.month,entry.day,
					entry.hour,entry.min,entry.sec,
					entry.FirstCluster,
					entry.size,
					(entry.subdir) ? "dir":"file");
			}
		}
	}

	else /*显示子目录*/
	{
		cluster_addr = DATA_OFFSET + (curdir->FirstCluster-2) * CLUSTER_SIZE ;
		if((ret = lseek(fd,cluster_addr,SEEK_SET))<0)
			perror("lseek cluster_addr failed");

		offset = cluster_addr;

		/*只读一簇的内容*/
		while(offset<cluster_addr +CLUSTER_SIZE)
		{
			ret = GetEntry(&entry);
			offset += abs(ret);
			if(ret > 0)
			{
				printf("%12s\t"
					"%d:%d:%d\t"
					"%d:%d:%d   \t"
					"%d\t"
					"%d\t\t"
					"%s\n",
					entry.short_name,
					entry.year,entry.month,entry.day,
					entry.hour,entry.min,entry.sec,
					entry.FirstCluster,
					entry.size,
					(entry.subdir) ? "dir":"file");
			}
		}
	}
	return 0;
} 


/*
*参数：entryname 类型：char
：pentry    类型：struct Entry*
：mode      类型：int，mode=1，为目录表项；mode=0，为文件
*返回值：偏移值大于0，则成功；-1，则失败
*功能：搜索当前目录，查找名为entryname的文件或目录项，如果没找到但会-1，找到了返回目标文件在硬盘中的偏移
*/
int ScanEntry (char *entryname,struct Entry *pentry,int mode)
{
	int ret,offset,i;
	int cluster_addr;
	char uppername[80];
	for(i=0;i< strlen(entryname);i++)
		uppername[i]= toupper(entryname[i]);
	uppername[i]= '\0';
	/*扫描根目录*/
	if(curdir ==NULL)  
	{
		if((ret = lseek(fd,ROOTDIR_OFFSET,SEEK_SET))<0)
			perror ("lseek ROOTDIR_OFFSET failed");
		offset = ROOTDIR_OFFSET;


		while(offset<DATA_OFFSET)
		{
			ret = GetEntry(pentry);
			offset +=abs(ret);

			if(pentry->subdir == mode &&!strcmp((char*)pentry->short_name,uppername))

				return offset;

		}
		return -1;
	}

	/*扫描子目录*/
	else  
	{
		cluster_addr = DATA_OFFSET + (curdir->FirstCluster -2)*CLUSTER_SIZE;
		if((ret = lseek(fd,cluster_addr,SEEK_SET))<0)
			perror("lseek cluster_addr failed");
		offset= cluster_addr;

		while(offset<cluster_addr + CLUSTER_SIZE)
		{
			ret= GetEntry(pentry);
			offset += abs(ret);
			if(pentry->subdir == mode &&!strcmp((char*)pentry->short_name,uppername))
				return offset;
		}
		return -1;
	}
}

void copyEntry(struct Entry* curdir, struct Entry* pentry){
	memcpy(curdir->short_name,pentry->short_name,11);
	memcpy(curdir->long_name,pentry->long_name,26);
	curdir->year = pentry->year;
	curdir->month = pentry->month;
	curdir->day = pentry->day;
	curdir->hour = pentry->hour;
	curdir->min = pentry->min;
	curdir->sec = pentry->sec;
	curdir->FirstCluster = pentry->FirstCluster;
	curdir->size = pentry->size;
}

/*
*参数：dir，类型：char
*返回值：1，成功；-1，失败
*功能：改变目录到父目录或子目录
*/
int fd_cd(char **dir,int mode,int num)
{
	struct Entry *pentry,*tmp;
	int ret,i;	

	if (mode == 0){
		for(i=0;i<num;i++){
			
			if(!strcmp(dir[i],"."))
				continue;
			if(!strcmp(dir[i],"..") && curdir==NULL){
				printf("no father dir!\n");
				return 1;
			}
			if(!strcmp(dir[i],"..") && curdir!=NULL)
			{
	 		 
				curdir = fatherdir[dirno];
				dirno--;
				printf("traceback : %s\n",curdir->short_name);
				continue;
			}
			
			pentry = (struct Entry*)malloc(sizeof(struct Entry));
			ret = ScanEntry(dir[i],pentry,1);		
			
			if(ret < 0)
			{
				printf("no such dir\n");
				free(pentry);
				return -1;
			}
			
			dirno ++;
			fatherdir[dirno] = curdir;

			curdir = (struct Entry*)malloc(sizeof(struct Entry));
			copyEntry(curdir, pentry);
			
			free(pentry);
		}
	}
	else{
		dirno ++;
		fatherdir[dirno]=curdir;
		curdir = NULL;
		for(i=0;i<num;i++){
			if(!strcmp(dir[i],".")){
				printf("\".\" is not allowed in absolute path\n");
				return 1;			
			}
			if(!strcmp(dir[i],".."))
			{
				printf("\"..\" is not allowed in absolute path\n");
				return 1;	
			}

			pentry = (struct Entry*)malloc(sizeof(struct Entry));
	
			ret = ScanEntry(dir[i],pentry,1);
			if(ret < 0)
			{
				printf("no such dir\n");
				free(pentry);
				return -1;
			}
			dirno ++;
			fatherdir[dirno] = curdir;

			curdir = (struct Entry*)malloc(sizeof(struct Entry));
			copyEntry(curdir, pentry);
			
			free(pentry);
		}
	}

	return 1;
}

/*
*参数：prev，类型：unsigned char
*返回值：下一簇
*在fat表中获得下一簇的位置
*/
unsigned short GetFatCluster(unsigned short prev)
{
	unsigned short next;
	int index;

	index = prev * 2;
	next = RevByte(fatbuf[index],fatbuf[index+1]);

	return next;
}

/*
*参数：cluster，类型：unsigned short
*返回值：void
*功能：清除fat表中的簇信息
*/
void ClearFatCluster(unsigned short cluster)
{
	int index;
	index = cluster * 2;
	//*2是因为我们用16bytes来表示一个cluster，这16字节是按照8字节8字节存入fatbuf中
	//所以每次更新要将连着的两个清0
	fatbuf[index]=0x00;
	fatbuf[index+1]=0x00;

}


/*
*将改变的fat表值写回fat表
*/
int WriteFat()
{
	int i=-1;
	for(i = 0 ; i < bdptor.FATs ; ++i)
	{	
		if(lseek(fd,FAT_ONE_OFFSET+i*bdptor.SectorsPerFAT * bdptor.BytesPerSector,SEEK_SET)<0)
		{
			perror("lseek failed");
			return -1;
		}
		if(write(fd,fatbuf,bdptor.SectorsPerFAT*bdptor.SectorsPerFAT)<0)
		{
			perror("read failed");
			return -1;
		}
	}
	return 1;
}

/*
*读fat表的信息，存入fatbuf[]中
*/
int ReadFat()
{
	if(lseek(fd,FAT_ONE_OFFSET,SEEK_SET)<0)
	{
		perror("lseek failed");
		return -1;
	}
	if(read(fd,fatbuf,bdptor.SectorsPerFAT*bdptor.SectorsPerFAT)<0)
	{
		perror("read failed");
		return -1;
	}
	return 1;
}


/*
*参数：filename，类型：char
*返回值：1，成功；-1，失败
*功能;删除当前目录下的文件
*/
int fd_df(char *filename)
{
	struct Entry *pentry;
	int ret;
	unsigned char c;
	unsigned short seed,next;

	pentry = (struct Entry*)malloc(sizeof(struct Entry));

	/*扫描当前目录查找文件*/
	ret = ScanEntry(filename,pentry,0);
	if(ret<0)
	{
		printf("no such file\n");
		free(pentry);
		return -1;
	}

	/*清除fat表项*/
	seed = pentry->FirstCluster;
	while((next = GetFatCluster(seed))!=0xffff)
	{
		ClearFatCluster(seed);
		seed = next;

	}

	ClearFatCluster( seed );

	/*清除目录表项*/
	c=0xe5;//e5表示该目录项可用

	//现将文件指针定位到目录处，0x20等价于32，因为每条目录表项32bytes
	if(lseek(fd,ret-0x20,SEEK_SET)<0)
		perror("lseek fd_df failed");
	//标记目录表项可用
	if(write(fd,&c,1)<0)
		perror("write failed");  

	/*
        这段话在源程序中存在，但助教感觉这句话是错的。。。o(╯□╰)o
        如果发现助教的感觉错了赶紧告诉助教，有加分！！
	if(lseek(fd,ret-0x40,SEEK_SET)<0)
		perror("lseek fd_df failed");
	if(write(fd,&c,1)<0)
	perror("write failed");*/

	free(pentry);
	if(WriteFat()<0)
		exit(1);
	return 1;
}


/*
*参数：filename，类型：char，创建文件的名称
size，    类型：int，文件的大小
*返回值：1，成功；-1，失败
*功能：在当前目录下创建文件
*/
int fd_cf(char *filename,int size)
{

	struct Entry *pentry;
	int ret,i=0,cluster_addr,offset;
	int year, month, day, hour, min, sec;
	time_t now;
	unsigned short cluster,clusterno[100];
	unsigned char c[DIR_ENTRY_SIZE];
	int index,clustersize;
	unsigned char buf[DIR_ENTRY_SIZE];
	struct tm *t;

	pentry = (struct Entry*)malloc(sizeof(struct Entry));


	clustersize = (size / (CLUSTER_SIZE));

	if(size % (CLUSTER_SIZE) != 0)
		clustersize ++;

	//扫描根目录，是否已存在该文件名
	ret = ScanEntry(filename,pentry,0);
	if (ret<0)
	{
		/*查询fat表，找到空白簇，保存在clusterno[]中*/
		for(cluster=2;cluster<1000;cluster++)
		{
			index = cluster *2;
			if(fatbuf[index]==0x00&&fatbuf[index+1]==0x00)
			{
				clusterno[i] = cluster;

				i++;
				if(i==clustersize)
					break;

			}

		}

		/*在fat表中写入下一簇信息*/
		for(i=0;i<clustersize-1;i++)
		{
			index = clusterno[i]*2;

			fatbuf[index] = (clusterno[i+1] &  0x00ff);
			fatbuf[index+1] = ((clusterno[i+1] & 0xff00)>>8);


		}
		/*最后一簇写入0xffff*/
		index = clusterno[i]*2;
		fatbuf[index] = 0xff;
		fatbuf[index+1] = 0xff;

		if(curdir==NULL)  /*往根目录下写文件*/
		{ 

			if((ret= lseek(fd,ROOTDIR_OFFSET,SEEK_SET))<0)
				perror("lseek ROOTDIR_OFFSET failed");
			offset = ROOTDIR_OFFSET;
			while(offset < DATA_OFFSET)
			{
			  //读取一个条目
				if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
					perror("read entry failed");

				offset += abs(ret);
				//看看条目是否可用（e5）或者是不是表示后面没有更多条目（00）
				if(buf[0]!=0xe5&&buf[0]!=0x00)
				{
				  //buf[11]是attribute，但是感觉下面这个while循环并没有什么卵用。。。
				  while(buf[11] == 0x0f)
					{
						if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
							perror("read root dir failed");
						offset +=abs(ret);
					}
				}


				/*找出空目录项或已删除的目录项*/ 
				else
				{       
					offset = offset-abs(ret);     
					for(i=0;i<=strlen(filename);i++)
					{
						c[i]=toupper(filename[i]);
					}			
					for(;i<=10;i++)
						c[i]=' ';

					c[11] = 0x01;

					time(&now);
					t = localtime(&now);
					year = t->tm_year - 80;
					month = t->tm_mon + 1;
					day = t->tm_mday;
					hour = t->tm_hour;
					min = t->tm_min;
					sec = t->tm_sec;
					/*修改时间*/
					c[22] = (((hour << 11) + (min << 5) + (sec >> 1)) & 0x00ff);
					c[23] = ((((hour << 11) + (min << 5) + (sec >> 1)) & 0xff00) >> 8);

					/*修改日期*/
					c[24] = (((year << 9) + (month << 5) + day) & 0x00ff);
					c[25] = ((((year << 9) + (month << 5) + day) & 0xff00) >> 8);

					/*写第一簇的值*/
					c[26] = (clusterno[0] &  0x00ff);
					c[27] = ((clusterno[0] & 0xff00)>>8);

					/*写文件的大小*/
					c[28] = (size &  0x000000ff);
					c[29] = ((size & 0x0000ff00)>>8);
					c[30] = ((size& 0x00ff0000)>>16);
					c[31] = ((size& 0xff000000)>>24);

					/*还有很多内容并没有写入，大家请自己补充*/
					/*而且这里还有个问题，就是对于目录表项的值为00的情况处理的不好*/
					if(lseek(fd,offset,SEEK_SET)<0)
						perror("lseek fd_cf failed");
					if(write(fd,&c,DIR_ENTRY_SIZE)<0)
						perror("write failed");

					free(pentry);
					if(WriteFat()<0)
						exit(1);

					return 1;
				}

			}
		}
		else 
		{
		  //子目录的情况与根目录类似
			cluster_addr = (curdir->FirstCluster -2 )*CLUSTER_SIZE + DATA_OFFSET;
			if((ret= lseek(fd,cluster_addr,SEEK_SET))<0)
				perror("lseek cluster_addr failed");
			offset = cluster_addr;
			while(offset < cluster_addr + CLUSTER_SIZE)
			{
				if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
					perror("read entry failed");

				offset += abs(ret);

				if(buf[0]!=0xe5&&buf[0]!=0x00)
				{
					while(buf[11] == 0x0f)
					{
						if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
							perror("read root dir failed");
						offset +=abs(ret);
					}
				}
				else
				{ 
					offset = offset - abs(ret);      
					for(i=0;i<=strlen(filename);i++)
					{
						c[i]=toupper(filename[i]);
					}
					for(;i<=10;i++)
						c[i]=' ';

					c[11] = 0x01;

					time(&now);
					t = localtime(&now);
					year = t->tm_year - 80;
					month = t->tm_mon + 1;
					day = t->tm_mday;
					hour = t->tm_hour;
					min = t->tm_min;
					sec = t->tm_sec;
					/*修改时间*/
					c[22] = (((hour << 11) + (min << 5) + (sec >> 1)) & 0x00ff);
					c[23] = ((((hour << 11) + (min << 5) + (sec >> 1)) & 0xff00) >> 8);

					/*修改日期*/
					c[24] = (((year << 9) + (month << 5) + day) & 0x00ff);
					c[25] = ((((year << 9) + (month << 5) + day) & 0xff00) >> 8);

					c[26] = (clusterno[0] &  0x00ff);
					c[27] = ((clusterno[0] & 0xff00)>>8);

					c[28] = (size &  0x000000ff);
					c[29] = ((size & 0x0000ff00)>>8);
					c[30] = ((size& 0x00ff0000)>>16);
					c[31] = ((size& 0xff000000)>>24);

					if(lseek(fd,offset,SEEK_SET)<0)
						perror("lseek fd_cf failed");
					if(write(fd,&c,DIR_ENTRY_SIZE)<0)
						perror("write failed");

					free(pentry);
					if(WriteFat()<0)
						exit(1);

					return 1;
				}
			}
		}
	}
	else
	{
		printf("This filename is exist\n");
		free(pentry);
		return -1;
	}
	return 1;

}

void do_usage()
{
	printf("please input a command, including followings:\n\
		ls\t\t\tlist all files\n\
		cd <dir>\t\tchange direcotry\n\
		cf <filename> <size>\tcreate a file\n\
		df <file>\t\tdelete a file\n\
		mkdir <dirname>\t\tcreate a directory\n\
		rmdir <dirname>\t\tdelete an empty directory\n\
		exit\t\t\texit this system\n");
}

void parse(char *dir){
	char** toSearch;
	int i=0,j=0,mode,p=0,k=0;
	toSearch = (char **)malloc(10*sizeof(char *));
	
	if (dir[0] == '/'){
		mode = 1;
		i++;
	}
	else
		mode = 0;
	for (;i<strlen(dir);i++){
		if (k==0){
			toSearch[p] = (char *) malloc (20 * sizeof(char));
		}	
		if (dir[i]!='/')
			toSearch[p][k++]=dir[i];
		else{
			toSearch[p][k]='\0';
			p++;
			k=0;
		}
	}
	toSearch[p][k]='\0';
	fd_cd(toSearch, mode, p+1);
}

int main()
{
	char input[10];
	int size=0;
	char name[12];
	if((fd = open(DEVNAME,O_RDWR))<0)
	  perror("open failed");//以可读写方式打开文件
	ScanBootSector();
	if(ReadFat()<0)
		exit(1);
	do_usage();
	while (1)
	{
		printf(">");
		scanf("%s",input);

		if (strcmp(input, "exit") == 0)
			break;
		else if (strcmp(input, "ls") == 0)
			fd_ls();
		else if(strcmp(input, "cd") == 0)
		{
			scanf("%s", name);
			//fd_cd(name);
			parse(name);
		}
		else if(strcmp(input, "df") == 0)
		{
			scanf("%s", name);
			fd_df(name);
		}
		else if(strcmp(input, "cf") == 0)
		{
			scanf("%s", name);
			scanf("%s", input);
			size = atoi(input);
			fd_cf(name,size);
		}
		else if (strcmp(input, "mkdir") == 0)
		{
			scanf("%s", name);
			JGmkdir(name);
		}
		else if (strcmp(input, "rmdir") == 0)
		{
			scanf("%s", name);
			YHrmdir(name, curdir);
		}
		else
			do_usage();
	}	

	return 0;
}
