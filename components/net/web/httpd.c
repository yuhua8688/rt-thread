#include "httpd.h"
/**************************************
*author: Jone.Chen   < yuhua8688@tom.com>
*License:LGPL
***************************************/


#ifdef RT_HTTP_USE_AUTH
volatile unsigned char http_user_auth;
#endif

#ifdef RT_HTTP_USE_CGI
extern void process_cgi(int sock, char cmd, char param, char *data, int len);
#endif

#ifdef RT_HTTP_USE_POST
extern void process_post(int sock, char *name, char *value);
#endif

#ifdef RT_HTTP_USE_UPLOAD
extern void process_upload(int sock, char *name, char *value, unsigned long offset, unsigned long length, unsigned long size);
#endif

#define  default_file_cnt 5
const char *g_default_file[] = {
  {".html"}, 
  {".htm"}, 
  {".shtml" }, 
  {".ssi" }, 
  {".shtm"}, 
};

#define  exe_file_cnt 4
const char *g_exe_file[] = {
  {".cgi"}, 
  {".shtml" }, 
  {".ssi" }, 
  {".shtm"}
};

rt_thread_t httpd_thread;

void init_httpd(void)
{
	httpd_thread = rt_thread_create("httpd", rt_httpd_entry, RT_NULL, RT_LWIP_HTTP_STACK_SIZE, RT_LWIP_HTTP_PRIORITY, 20);
	rt_thread_startup(httpd_thread);

#ifdef RT_HTTP_USE_AUTH
	http_user_auth = 0;
#endif

}

void rt_httpd_entry(void *parameter)
{
	int sock = 0;
	int sock_clt = 0;
	int recv_len = 0;
	socklen_t clt_len = 0;
	struct sockaddr_in srv_addr;
	struct sockaddr_in client_addr;
	char dbuf[WEB_BUFF_SIZE];

	sock = socket(AF_INET, SOCK_STREAM, 0);
	srv_addr.sin_family      = PF_INET;
	srv_addr.sin_port        = htons(80);
	srv_addr.sin_addr.s_addr	 =  INADDR_ANY;
	rt_thread_delay(300);
	bind(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	listen(sock, 30);
	rt_memset(dbuf, 0, WEB_BUFF_SIZE);
	while (1)
	{
		sock_clt = accept(sock, (struct sockaddr*)&client_addr, &clt_len);
		recv_len = recv(sock_clt, dbuf, WEB_BUFF_SIZE, 0);	//
		dbuf[recv_len] = 0;
		if (recv_len > 0)
		{
			resolv_http(sock_clt, dbuf, recv_len);
		}

		closesocket(sock_clt);
		rt_memset(&client_addr, 0, sizeof(client_addr));
		clt_len = 0;
		rt_thread_delay(1);  
	}

}

#ifdef RT_HTTP_USE_AUTH
/*
HTTP/1.1 401 Unauthorized 
Server: Web Auth 
Content-Type: text/html; charset = GB2312
Accept-Ranges: bytes
Connection: close
Cache-Control: no-cache, no-store
WWW-Authenticate: Basic realm = "Router Web Config System"

 < HTML> < HEAD> < TITLE>401 Unauthorized < /TITLE> < /HEAD> < BODY> < H2>401 Unauthorized < /H2>Authorization failed. < /BODY> < /HTML>
*/
unsigned char user_auth(int sock)
{
	if (http_user_auth  !=  1)
	{
		send(sock, "HTTP/1.1 401 Unauthorized\r\nServer: Web Auth\r\nContent-Type: text/html; charset = GB2312\r\nAccept-Ranges: bytes\r\nConnection: close\r\n", 127, 0);
		send(sock, "Cache-Control: no-cache, no-store\r\nWWW-Authenticate: Basic realm = \"", 65, 0);
		send(sock, USER_AUTH_TITLE, strlen(USER_AUTH_TITLE), 0);
		send(sock, "\"\r\n\r\n < HTML> < HEAD> < TITLE>401 Unauthorized < /TITLE> < /HEAD> < BODY> < H2>401 Unauthorized < /H2>Authorization failed. < /BODY> < /HTML>", 121, 0);

		return 1;
	}

	return 0;
}

unsigned char *decode_base_64(unsigned char *src)
{
	unsigned int tmp;
	unsigned int i;
	unsigned int len;	
	unsigned char j;
	unsigned char *base;

	len = strlen((char*)src);	
	base = (unsigned char*) rt_malloc( (len / 4) * 3 + 1);
	memset(base, 0, (len / 4) * 3 + 1);
	for (i = 0; i < len; i  +=  4)
	{
		tmp = 0;
		for (j = 0; j < 4; j ++)
		{
			if (src[i + j] >= 'A' && src[i + j]  < = 'Z')
			{
				tmp = tmp  < < 6;
				tmp = tmp + (src[i + j] - 'A');
			}
			else if (src[i + j] >= 'a' && src[i + j]  < = 'z')
			{
				tmp = tmp  < < 6;
				tmp = tmp + (src[i + j] - 'a' + 26);
			}
			else if (src[i + j] >= '0' && src[i + j]  < = '9')
			{
				tmp = tmp  < < 6;
				tmp = tmp + (src[i + j] - '0' + 52);
			}
			else if (base[i + j] == '+')
			{
				tmp = tmp  < < 6;
				tmp = tmp + 62;
			}
			else if (base[i + j] == '/')
			{
				tmp = tmp  < < 6;
				tmp = tmp + 63;	
			}
			else 
			{
				tmp = tmp  < < 6;
			}
		}

		base[i / 4 * 3 + 2] = tmp & 0xFF;
		tmp = tmp >> 8;
		base[i / 4 * 3 + 1] = tmp & 0xFF;
		tmp = tmp>>8;
		base[i/4*3] = tmp&0xFF;
	}

	return base;
}
#endif

void err_404(int sock){
	send(sock, "404 file not found", 18, 0);
}

#ifdef RT_HTTP_USE_CGI
void parse_file_cgi(int sock, int fd, char *cgi_data)	
{

 	int tfd;
	int size = 0;
	int len = 0;
	int tmp = 0;
	int cur_offset = 0;
	int offset = 0;
	int i_len=0;
	int i_offset=0;
	int i_cur_offset=0;
	char cmd = 0;
	char param = 0;
	char *ptr = RT_NULL;
	char *ptr_tmp = RT_NULL;
 	char *i_buff = &cgi_data[WEB_BUFF_SIZE/2];//�����ڴ�ʹ�ù��������send��������

	if (fd < 0)
	{
		err_404(sock);
		return ;
	}
	
	offset = lseek(fd, 0, SEEK_END);
	cur_offset = lseek(fd, 0, SEEK_SET);
	while (cur_offset < offset)
	{
		rt_memset(cgi_data, 0, WEB_BUFF_SIZE/2);
		size = read(fd, cgi_data, WEB_BUFF_SIZE/2);
		ptr = cgi_data;
		len = 0;
		while (len < size)
		{	
			rt_thread_delay(1);
			ptr_tmp = strchr(ptr, '\n');
			if (ptr_tmp == RT_NULL)
			{
			   break;
			}
			else
			{

				switch(*ptr){
				 case '#':
					ptr_tmp++;
					tmp = ptr_tmp-ptr;
					len += tmp;
					ptr = ptr_tmp;
				 	break;
				 case 'c':
				 	cmd = *(ptr+2);
					param = *(ptr+4);
					ptr += 6;
					ptr_tmp++;
					tmp = ptr_tmp-ptr;
					rt_memset(&cgi_data[WEB_BUFF_SIZE/2], 0, WEB_BUFF_SIZE/2);
					rt_memcpy(&cgi_data[WEB_BUFF_SIZE/2], ptr, tmp);
					process_cgi(sock, cmd, param, &cgi_data[WEB_BUFF_SIZE/2], tmp);
					tmp += 6;
					len += tmp;	
					ptr = ptr_tmp;
				 	break;
				 case 'i':
				 {
				 	ptr += 2;//file name
					tmp = ptr_tmp-ptr;
					
					rt_memset(i_buff, 0, tmp + 1);//��һλ�������
					if (*ptr == '/')
					{
						rt_memcpy(i_buff, ptr, tmp - 1);
					}
					else
					{
						*i_buff = '/';
						rt_memcpy(i_buff+1, ptr, tmp - 1);
					}

					tmp += 1;
					len += tmp;
					ptr = ptr_tmp + 1;
					tfd = open(i_buff, O_RDONLY, 400);
					if (tfd < 0)
					{
					   err_404(sock);
					   return;
					}

					i_offset = lseek(tfd, 0, SEEK_END);
					i_cur_offset = lseek(tfd, 0, SEEK_SET);
					while (i_cur_offset < i_offset)
					{
					  i_len = read(tfd, i_buff, WEB_BUFF_SIZE / 2);
					  send(sock, i_buff, i_len, 0);
					  i_cur_offset = lseek(tfd, 0, SEEK_CUR);
					}
					close(tfd);
				 }
				 break;
				 case 't':
				 	ptr += 2;//file name
					ptr_tmp ++;
					tmp = ptr_tmp - ptr;
					len = len + tmp + 2;
					send(sock, ptr, tmp, 0);
					ptr = ptr_tmp;
				 	break;
				case '.':
					return;
				 default:
				 	  ptr ++;
					  len ++;
					  break;
				}//end switch
			}
			
		}//end while
		
		if (cur_offset < offset)
		{
			cur_offset = lseek(fd, cur_offset + len, SEEK_SET);
		}
		else
		{
		 	cur_offset = lseek(fd, 0, SEEK_CUR);
		}
	}
}
#endif

char open_url(int sock, char *url, char *param){
	int fd;
	int len;
	int offset;
	int cur_offset;
	int i = 0;
	 
#ifdef RT_HTTP_USE_AUTH
	if (user_auth(sock))
	{
	 	return 0;
	}
#endif

	if (url[0] == '/' &&url[1] == 0)
	{
		for(i = 0;i < default_file_cnt;i++)
		{
			sprintf(param, "/index%s\0", g_default_file[i]);
			fd = open(param, O_RDONLY, 400);
			if (fd >= 0)
			{
				break;
			}
		}
	}
	else
	{
	  fd = open(url, O_RDONLY, 400);
	}

	if (fd >= 0)
	{	
		for(i = 0;i < exe_file_cnt;i++)
		{			
			if (strstr(url, g_exe_file[i]) != RT_NULL)
			{
				break;
			}
		}

		if (i < exe_file_cnt)
		{
			if (strstr(url, ".cgi") != RT_NULL)
			{ //cgi����

#ifdef RT_HTTP_USE_CGI
				send(sock, "HTTP/1.1 200 OK\r\nServer: lwIP\r\nContent-type: text/html\r\n\r\n", 58, 0);
				parse_file_cgi( sock, fd, param);	
#endif

			}
			else
			{//ssi����
		
			}
		}
		else
		{
			offset = lseek(fd, 0, SEEK_END);
			cur_offset = lseek(fd, 0, SEEK_SET);
			send(sock, "HTTP/1.1 200 OK\r\nServer: lwIP\r\nContent-type: ", 45, 0);//
			if (strstr(url, ".ht") != RT_NULL)
			{
			 	send(sock, "text/html", 9, 0);
			}
			else if (strstr(url, ".sht") != RT_NULL)
			{
			    send(sock, "text/html\r\nExpires: Fri, 10 Apr 2008 14:00:00 GMT\r\nPragma: no-cache", 67, 0);
			}
			else if (strstr(url, ".css") != RT_NULL)
			{
				 send(sock, "text/css", 8, 0);
			}
			else if (strstr(url, ".js") != RT_NULL)
			{
				 send(sock, "application/x-javascript", 24, 0);
			}
			else if (strstr(url, ".jpg") != RT_NULL)
			{
				 send(sock, "image/jpeg", 10, 0);
			}
			else if (strstr(url, ".png") != RT_NULL)
			{
				 send(sock, "image/png", 9, 0);
			}
			else if (strstr(url, ".bmp") != RT_NULL)
			{
				 send(sock, "image/bmp", 9, 0);
			}
			else if (strstr(url, ".gif") != RT_NULL)
			{
				 send(sock, "image/gif", 9, 0);
			}

			else if (strstr(url, ".ico") != RT_NULL)
			{
				 send(sock, "image/x-icon", 12, 0);
			}

			else if (strstr(url, ".swf") != RT_NULL)
			{
				 send(sock, "application/x-shockwave-flash", 29, 0);
			}
			else if (strstr(url, ".xml") != RT_NULL)
			{
				 send(sock, "text/xml\r\nExpires: Fri, 10 Apr 2008 14:00:00 GMT\r\nPragma: no-cache", 66, 0);
			}
			else if (strstr(url, ".txt") != RT_NULL)
			{
				 send(sock, "text/plain", 10, 0);
			}
			else if (strstr(url, ".class") != RT_NULL)
			{
				 send(sock, "application/octet-stream", 24, 0);
			}
			else if (strstr(url, ".ram") != RT_NULL)
			{
				 send(sock, "audio/x-pn-realaudio", 20, 0);
			}

			send(sock, "\r\n\r\n", 4, 0);
			while (cur_offset < offset)
			{
				len = read(fd, param, WEB_BUFF_SIZE);
				send(sock, param, len, 0);
				cur_offset = lseek(fd, 0, SEEK_CUR);
			}
		}

		close(fd);
	}
	else
	{
		err_404(sock);
	}

	return 0;
}



void resolv_http(int sock, char *data, int len)
{
	char *p_name = RT_NULL; 
	char *p_value = RT_NULL;
	char *param = RT_NULL; 
	char *url = RT_NULL;

#ifdef RT_HTTP_USE_UPLOAD	 
	char *p_start = RT_NULL;
	unsigned long offset;
	unsigned long stream_size = 0;
	unsigned char boundary_len = 0;
#endif

#ifdef RT_HTTP_USE_AUTH
	char *user_pwd;
#endif

	if (strncmp(data, "GET ", 4) == 0)
	{
		url = data+4;
		param = strstr(data, "HTTP");
		param--;
		*param = 0;

#ifdef RT_HTTP_USE_AUTH
		if (http_user_auth != 1) //url[0] == '/' && url[1] == 0
		{
			param++;
			param = strstr(param, "Authorization");
			if (param != RT_NULL)
			{
			   param = param + 21;
			   user_pwd = param;
			   param = strstr(param, "\r\n");
			   *param = 0;
			   user_pwd = (char *) decode_base_64((unsigned char *)user_pwd);
			   param = strstr(user_pwd, ":");
			   *param = 0;
			   param++;//pwd
			   if (strcmp(user_pwd, USER_AUTH_NAME) == 0 &&strcmp(param, USER_AUTH_PWD) == 0)
			   {
			   	  http_user_auth = 1;
			   }
			   else
			   {
			   	  http_user_auth = 0;
			   } 

				rt_free(user_pwd);
			}
		}
#endif

		url = rt_strdup(url);
		open_url(sock, url, data);
		rt_free(url);
	}
	else if (strncmp(data, "POST ", 5) == 0)
	{	
		param = strstr(data, "Content-Type");
		param += 14;//�ж�type
		if (*param == 'a' || *param == 'A')//type:application/x-www-form-urlencoded
		{

#ifdef RT_HTTP_USE_POST		 
			param = strstr(param, "\r\n\r\n");//����λ��
			param += 4;
			while (param != RT_NULL)
			{
				p_name = param;
				param = strchr(param, '=');
				if (param != RT_NULL)
				{
					*param = 0;
					param++;
					p_value = param;
					param = strchr(param, '&');
					if (param != RT_NULL)
					{
						*param = 0;
						param++;
					}
					
					if (p_name != RT_NULL && p_value != RT_NULL)
					{
						process_post( sock, p_name, p_value);
					}
					
				}
			}

			url = data+5;
			param = strstr(data, "HTTP");
			param--;
			*param = 0;
			url = rt_strdup(url);
			open_url( sock, url, data);
			rt_free(url);
#endif

		}
		else if (*param == 'm' || *param == 'M')	//type:multipart/form-data
		{

#ifdef RT_HTTP_USE_UPLOAD
			url = data+5;
			param = strstr(data, "HTTP");
			param--;
			*param = 0;			
			param = param+1;
			url = rt_strdup(url);
			param = strstr(param, "boundary");//�߽糤�� = 
			param = param+9;
			p_start = param;
			param = strstr(param, "\r\n");
			boundary_len = param-p_start;

			param = strstr(param, "Length");//�ļ�����
			param = param+8;
			p_start = param;
			param = strstr(param, "\r\n\r\n");
			stream_size = 0;
			while (p_start < param){
			  stream_size = stream_size*10+(*p_start-'0');
			  p_start++;
			}

			param = param+4;
			p_start = param;//��ʼ���ļ�����λ��	
			param = strstr(param, "filename");//����λ��
			param += 10;
			p_name = param;
			param = strchr(param, '"');
			*param = 0;
			param += 1;
			param = strstr(param, "\r\n\r\n");//����λ��
			param += 4;
			p_value = param;
			stream_size = stream_size - (p_value - p_start);//����ʣ���ֽ�
			stream_size = stream_size - boundary_len - 8;//�����ļ�����
			len = len - (p_value - data);//�������buffʣ�೤��
			p_start = param;
			len = len > stream_size ? stream_size : len;
			p_name = rt_strdup(p_name);
			offset = 0;
			process_upload(sock, p_name, p_value, offset, len, stream_size - len);	
			offset = offset + len;
			stream_size = stream_size - len;
			if (stream_size)
			{ 
				while (stream_size >= 1)
				{	
					len = recv(sock, data, WEB_BUFF_SIZE, 0);	//
					data[len] = 0;
					if (len >= stream_size)
					{
						process_upload(sock, p_name, data, offset, stream_size, 0);
						offset = offset+stream_size;
						stream_size = 0;
					}
					else
					{
						process_upload(sock, p_name, data, offset, len, stream_size-len);
						offset = offset+len;
						stream_size = stream_size-len;
					}
				}
			}

			rt_free(p_name);
			open_url( sock, url, data);
			rt_free(url);
#endif
		}
	}
	else
	{

	 return ;
	}
}

