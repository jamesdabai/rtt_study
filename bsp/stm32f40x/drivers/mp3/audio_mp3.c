#include <string.h>
#include "audio_mp3.h"
#include "mp3dec.h"
#include "music.h"

#define BigEndianTab2LittleEndianInt(tab) (((unsigned int)(tab)[0]<<24) + ((unsigned int)(tab)[1]<<16) +((unsigned int)(tab)[2]<<8) + ((tab)[3]))

uint8_t MP3_INPUT_BUFFER[MP3_INPUT_BUFFER_SIZE];//����buffer

/*
�����������Ϊ˫����
���м�һ��Ԫ�ؿ�ʼ ���临�Ƶ��������������
Ȼ�󱻸��Ƶ�Ԫ����ǰ�ƶ�1 ����Ŀ����ǰ�ƶ�2
���Է�ֹǰ��δ�����Ƶ����ݵ���
    (i==0)                   (i = size-1)
�������С�����        �������������������Щ�������
��	   ��    ��        ��                ��      ��
*[0]  ��  *[1]      *[2]       [3]     [4]     [5]
            ��        ��        ��
            �����������ة���������
                    (i--)
*/
void MonoChannelFill(uint16_t* buf,uint16_t size)
{
	uint16_t i;
	
	i = size-1;
	
	do
	{
		buf[2*i] = buf[i];//Ŀ��Ԫ�صĵ�һ����
		buf[2*i + 1] = buf[i];//Ŀ��Ԫ�صĵڶ�����
		i --;
	}
	while(i);
}

//����ID3V1
//option:�Ƿ�д����Ϣ ���Ϊ0ֻ�ǲ��Ҵ治����
AudioPlayRes MP3_GetID3v1(int fd,MP3_Info *pctrl,unsigned char option)
{
	ID3V1_Tag *id3v1tag;
	uint32_t br;
	
	lseek(fd,-128,SEEK_END);
	//f_lseek(file,f_size(file)-128);//ƫ�Ƶ��ļ�����128�ֽڴ�
	br = read(fd,MP3_INPUT_BUFFER,128-(4+30+1));//��ȡ�ļ� ������� ��ע������
	
	id3v1tag = (ID3V1_Tag*)MP3_INPUT_BUFFER;
	
	if(strncmp("TAG",(char*)id3v1tag->id,3)==0)//��MP3 ID3V1 TAG
	{
		return AudioPlay_OK;
	}
	else
	{
		return AudioPlay_MP3_NoID3v1Tag;
	}
}

AudioPlayRes MP3_GetID3v2(int fd,MP3_Info *pMP3info)
{
	ID3V2_TagHead *taghead;
	uint32_t br;
	uint32_t id3v2size;//֡��С
	
	lseek(fd,0,SEEK_SET);//��λ���ļ���ͷ
	br = read(fd,MP3_INPUT_BUFFER,sizeof(ID3V2_TagHead));//��ȡID3V2��ǩͷ
	taghead = (ID3V2_TagHead*)MP3_INPUT_BUFFER;//������ǩͷ
	
	if(strncmp("ID3",(char*)taghead->id,3) == 0)//ǰ3���ַ�Ϊ"ID3"
	{
		id3v2size=((u32)taghead->size[0]<<21)|((u32)taghead->size[1]<<14)|((u16)taghead->size[2]<<7)|taghead->size[3];//�õ�tag��С 4���ֽ��Ǵ��ģʽ ֻȡ��7λ
		
		pMP3info->DataStartOffset = id3v2size;//�õ�mp3���ݿ�ʼ��ƫ����
	}
	else
	{
		pMP3info->DataStartOffset = 0;//������ID3,mp3�����Ǵ�0��ʼ
		return AudioPlay_MP3_NoID3v2Tag;
	}
	
	return AudioPlay_OK;
}

//��ȡMP3������Ϣ
//pname:MP3�ļ�·��
//pctrl:MP3������Ϣ�ṹ�� 
//����ֵ:0,�ɹ�
//    ����,ʧ��
AudioPlayRes MP3_GetInfo(int fd,MP3_Info* pMP3info)
{
	MP3FrameInfo frame_info;
	HMP3Decoder decoder;
	MP3_FrameXing* fxing;
	MP3_FrameVBRI* fvbri;
	AudioPlayRes res = AudioPlay_OK;
	
	unsigned char mp3_id3_present = 0;//�Ƿ����ID3V2
	int offset = 0;//֡ͬ�������������ͷ��ƫ��
    int br;
	short samples_per_frame;//һ֡�Ĳ�������
	unsigned int p;//����ʶ��VBR���ݿ��ָ��
	unsigned int filesize;//�ļ���С
	unsigned int totframes;//��֡��
	
	filesize = lseek(fd,0,SEEK_END);//�õ�MP3�ļ���С
	
	if(!MP3_GetID3v2(fd,pMP3info))//����ID3V2����
	{
		mp3_id3_present = 1;//����ID3V2
		filesize -= pMP3info->DataStartOffset;//�������ID3V2 ��ȥ���Ĵ�С
	}
	
	if(!MP3_GetID3v1(fd,pMP3info,!mp3_id3_present))//����ID3V1���� ����������ID3V2
	{
		filesize -= 128;//�������ID3V1 ��ȥ���Ĵ�С
	}
	
	pMP3info->DataSize = filesize;
	
	lseek(fd,pMP3info->DataStartOffset,SEEK_SET);//ƫ�Ƶ����ݿ�ʼ�ĵط�
	br = read(fd,MP3_INPUT_BUFFER,MP3_INPUT_BUFFER_SIZE);//����������
	
	decoder = MP3InitDecoder();//MP3���������ڴ�
	offset = MP3FindSyncWord(MP3_INPUT_BUFFER,br);//����֡ͬ����Ϣ
	if(offset >= 0 && MP3GetNextFrameInfo(decoder,&frame_info,&MP3_INPUT_BUFFER[offset])==0)//�ҵ�֡ͬ����Ϣ����һ֡��Ϣ��ȡ����	
	{
		p = offset+4+32;//����֡ͷ(4bytes)��ͨ����Ϣ(32bytes) ƫ�Ƶ�������
		fvbri = (MP3_FrameVBRI*)(MP3_INPUT_BUFFER+p);//VBR�ļ�ͷλ�ڵ�һ����Ч֡��������
		
		if(!strncmp("VBRI",(char*)fvbri->id,4))//����VBRI֡
		{
			if(frame_info.version == MPEG1)
			{
				samples_per_frame = 1152;//MPEG1,layer3ÿ֡����������1152
			}
			else
			{
				samples_per_frame = 576;//MPEG2/MPEG2.5,layer3ÿ֡����������576
			}
			
			totframes = BigEndianTab2LittleEndianInt(fvbri->frames);//�õ���֡��
			pMP3info->TotalSec = totframes * samples_per_frame / frame_info.samprate;//�õ��ļ��ܳ��� ��֡������ÿ֡�Ĳ��������Բ�����
		}
		else//����VBRI֡,�����ǲ���Xing֡����Info֡
		{
			if(frame_info.version==MPEG1)//MPEG1 
			{
				p = (frame_info.nChans==2) ? 32:17;//����������ȷ��ƫ����
				samples_per_frame = 1152;//MPEG1,layer3ÿ֡����������1152
			}
			else
			{
				p = (frame_info.nChans==2) ? 17:9;
				samples_per_frame = 576;//MPEG2/MPEG2.5,layer3ÿ֡����������576
			}
			
			p += (offset+4);
			fxing = (MP3_FrameXing*)(MP3_INPUT_BUFFER + p);
			
			if(!strncmp("Xing",(char*)fxing->id,4) || !strncmp("Info",(char*)fxing->id,4))//��Xing֡����Info֡
			{
				if(fxing->flags[3] & 0x01)//������֡���ֶ�
				{
					totframes = BigEndianTab2LittleEndianInt(fxing->frames);//�õ���֡��
					pMP3info->TotalSec = totframes * samples_per_frame / frame_info.samprate;//�õ��ļ��ܳ��� ��֡������ÿ֡�Ĳ��������Բ�����
				}
				else//��������frames�ֶ�
				{
					pMP3info->TotalSec = filesize/(frame_info.bitrate/8);//ʹ���ļ���С���м���
				}
			}
			else//CBR��ʽ,ֱ�Ӽ����ܲ���ʱ��
			{
				pMP3info->TotalSec=filesize/(frame_info.bitrate/8);
			}
		}
		
		pMP3info->Bitrate = frame_info.bitrate;//�õ���ǰ֡������
		pMP3info->Samplerate = frame_info.samprate;//�õ�������
		pMP3info->Channels = frame_info.nChans;//������
		
		if(frame_info.nChans==2)
		{
			pMP3info->SampleSize = frame_info.outputSamps;//���PCM��������С
		}
		else
		{
			pMP3info->SampleSize = frame_info.outputSamps*2;//���PCM��������С,���ڵ�����MP3,ֱ��*2,����Ϊ˫�������
		}
	}
	else
	{
		res = AudioPlay_UnsupportedFormat;
	}
	
	MP3FreeDecoder(decoder);//�ͷ��ڴ�	
	return res;
}

AudioPlayRes MP3_Play(char* path)
{
	MP3_Info __MP3Info;
	AudioPlayRes res = AudioPlay_OK;
	HMP3Decoder mp3decoder = NULL;
	MP3FrameInfo mp3frameinfo;
	uint8_t* readptr;//MP3�����ָ��
	uint16_t* pOutputBuffer;//ָ�������������ָ��
	int32_t offset,bytesleft;//buffer��ʣ�����Ч����
	int br;
	int fd = -1;
	MP3_Info* MP3Info;
	
	MP3Info = &__MP3Info;
	memset(MP3Info,0,sizeof(MP3_Info));
	fd = open(path,O_RDONLY);
	if(fd == -1)//���ļ�
	{
	    rt_kprintf("open file fail\n");
		res =  AudioPlay_OpenFileError;//���ļ�����
	}
	else//�򿪳ɹ�
	{
	    //rt_kprintf("open file ok\n");
		res = MP3_GetInfo(fd,MP3Info);//��ȡ�ļ���Ϣ
		if(!res)
		{			
			AudioPlayInfo.FileType = AudioFileType_MP3;
			AudioPlayInfo.Channels = MP3Info->Channels;
			
			AudioPlayInfo.TotalSec = MP3Info->TotalSec;
			AudioPlayInfo.Samplerate = MP3Info->Samplerate;
			AudioPlayInfo.Bitrate = MP3Info->Bitrate/1000;
			AudioPlayInfo.BufferSize = MP3Info->SampleSize*2;
			
			AudioPlayInfo.Flags |= AUDIO_FLAG_INFO_READY;
			//AudioPlay_Init();//xqy���ӣ�ADCѡ��ʱ��ΪADCת������Դ
			if(AudioPlay_Config(16,MP3Info->Samplerate,MP3Info->SampleSize))//����I2S
			{
				res = AudioPlay_UnsupportedParameter;
			}
		}
	}
	if(res == AudioPlay_OK)
	{
		mp3decoder = MP3InitDecoder();//MP3��������ʼ��
		lseek(fd,MP3Info->DataStartOffset,SEEK_SET);//�����ļ�ͷ��tag��Ϣ
		br = read(fd,MP3_INPUT_BUFFER,MP3_INPUT_BUFFER_SIZE);
		if(br == -1)//�����������
		{
			res = AudioPlay_ReadFileError;
		}
		bytesleft = br;//��ȡ�����ݶ�����Ч����
		readptr = MP3_INPUT_BUFFER;//MP3��ָ��ָ��buffer�׵�ַ
		Play_Start();
		while(res == AudioPlay_OK)//û�г��������쳣(���ɷ��ҵ�֡ͬ���ַ�)
		{
			offset = MP3FindSyncWord(readptr,bytesleft);//��readptrλ��,��ʼ����ͬ���ַ�
			
			if(offset < 0)//û���ҵ�ͬ���ַ� ���Ž���Ҳ�����˴�����ѭ��
			{
				res = AudioPlay_PlayEnd;
				rt_kprintf("����֡����ѭ��\n");
				break;//����֡����ѭ��
			}
			else//�ҵ�ͬ���ַ���
			{
				readptr += offset;//MP3��ָ��ƫ�Ƶ�ͬ���ַ���
				bytesleft -= offset;//buffer�������Ч���ݸ��� ֡ͬ��֮ǰ��������Ϊ��Ч���ݼ�ȥ
				
				MP3Info->CurrentSec = MP3Info->TotalSec * (lseek(fd,0,SEEK_CUR) - MP3Info->DataStartOffset) / MP3Info->DataSize;//���㲥��ʱ��
				
				AudioPlayInfo.CurrentSec = MP3Info->CurrentSec;
				/*�û�������*/	
				{
				    rt_err_t err;
				    err = rt_sem_trytake(music_break);
				    if(err==RT_EOK)
				    {
				        //rt_kprintf("exiting music player\n");
				        break;
				    }
				}
				rt_sem_take(dma_int,RT_WAITING_FOREVER);
				rt_sem_control(dma_int,RT_IPC_CMD_RESET,0);//���ź�������
				pOutputBuffer = AudioPlay_GetCurrentBuff();//����õ�ǰ���л������ĵ�ַ
				if(MP3Decode(mp3decoder,&readptr,&bytesleft,(short*)pOutputBuffer,0))//����һ֡MP3���� ֱ�ӽ��뵽I2S�Ļ������� ��Լ�ڴ�
				{
				    uart_printf("�������\n");
					res = AudioPlay_DamagedFile;//���������� ����֡����ѭ��
				}
				AudioPlay_DataProc(pOutputBuffer,MP3Info->SampleSize);
				
				MP3GetLastFrameInfo(mp3decoder,&mp3frameinfo);//�õ��ոս����MP3֡��Ϣ
				if(MP3Info->Bitrate != mp3frameinfo.bitrate)//��������
				{
					MP3Info->Bitrate = mp3frameinfo.bitrate;
					AudioPlayInfo.Bitrate = mp3frameinfo.bitrate;//���±�����
				}
				
				if(mp3frameinfo.nChans == 1)//����ǵ�����
				{
					MonoChannelFill(pOutputBuffer,mp3frameinfo.outputSamps/2);//���������Ϊ������
				}
				
				if(bytesleft < MAINBUF_SIZE*2)//����������С��2��MAINBUF_SIZE��ʱ��,���벹���µ����ݽ���
				{
					memmove(MP3_INPUT_BUFFER,readptr,bytesleft);//�������������ʣ��Ĳ����Ƶ�ǰ��
					br = read(fd, MP3_INPUT_BUFFER+bytesleft, MP3_INPUT_BUFFER_SIZE-bytesleft);
					if(br == -1)//�������µ�����
					{
						res = AudioPlay_ReadFileError;
					}
					
					if(br < MP3_INPUT_BUFFER_SIZE-bytesleft)//�����ȡ����������������ݻ�����
					{
						memset(MP3_INPUT_BUFFER+bytesleft+br, 0, MP3_INPUT_BUFFER_SIZE-bytesleft-br);//��0
					}
					
					bytesleft = MP3_INPUT_BUFFER_SIZE;//��λ��ȡָ���ʣ���ֽ�
					readptr = MP3_INPUT_BUFFER;
				}
			}
		}
	}
	MP3FreeDecoder(mp3decoder);
	Play_Stop();
	close(fd);
	return res;
}