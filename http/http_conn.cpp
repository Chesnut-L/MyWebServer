#include "http_conn.h"

// 所有的socket上的事件都被注册到同一个epoll对象中
int http_conn::m_epollfd = -1;
// 统计用户数量
int http_conn::m_user_count = 0;
// 资源路径
const char* doc_root = "/home/tortoise/Codes/MyWebServer/resources";

http_conn::http_conn(){}

http_conn::~http_conn(){}

// 设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 添加文件描述符到epoll中 
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    // 触发模式 默认为LT 水平触发 
    event.events = EPOLLIN | EPOLLRDHUP;
    // event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    
    // 使用 EPOLLONESHOT事件 实现每个socket只被一个线程度读取
    if (one_shot) {
        event.events | EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改epoll中文件描述符 重置socket上的EPOLLONESHOT事件，确保下一次可读时，EPOLLIN事件可以被触发
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    // 触发模式 默认为LT 水平触发 
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化新接收的连接
void http_conn::init(int sockfd, const sockaddr_in & sockaddr){
    m_sockfd = sockfd;
    m_address = sockaddr;

    // 设置sockfd端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll中
    addfd(m_epollfd, m_sockfd, true);
    // 总用户数+1
    m_user_count++;

    init();
}

// 初始化连接的其他信息
void http_conn::init(){

    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化状态为解析请求首行
    m_checked_index = 0;                        
    
    m_start_line = 0;
    m_write_idx = 0;      
    m_read_idx = 0;
    
    m_method = GET;
    m_linger = false;   // 默认不保持连接
    m_url = 0;
    m_version = 0;
    m_content_len = 0;
    m_host = 0;
    
    
    // 清空缓冲区
    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_write_buf,WRITE_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);
}

// 关闭连接
void http_conn::close_conn(){
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 客户数量-1
    }
}


// 非阻塞的读 一次性读取所有数据
bool http_conn::read(){
    // printf("read all\n");
    // 缓冲区已满
    if ( m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }

    // 已经读取到的字节
    int bytes_read = 0;

    // 循环读取客户数据，直到无数据可读或对方关闭连接
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        // printf("reading, bytes_read : %d\n", bytes_read);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK){   // 表示没有数据了
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            // 对方关闭连接
            return false;
        }

        // 读到了数据
        m_read_idx += bytes_read;   // 更新索引值
    }

    printf("读取到了数据：\n %s\n", m_read_buf);

    return true;
}

// 主状态机 解析HTTP请求 
http_conn::HTTP_CODE http_conn::process_read(){

    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    
    // 解析到一行完整数据 或 解析到请求体并且也是完整的数据
    while ( (line_status == parse_line() && line_status == LINE_OK) || (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ) {
        // 获取一行数据
        text = get_line();
        printf("got 1 http line: %s\n", text);
        // 更新要检测的数据的起始位置
        m_start_line = m_checked_index;
        // 
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                // 解析请求行
                ret = parse_request_line(text);
                // 语法错误
                if (ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                // 解析请求头
                ret = parse_headers(text);
                if (ret == BAD_REQUEST){
                    return BAD_REQUEST;
                } 
                else if (ret == GET_REQUEST) {
                    // 解析到完整请求头
                    return do_request();
                }
            }

            case CHECK_STATE_CONTENT:
            {
                // 解析请求体
                ret = parse_content(text);
                if (ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

// 解析HTTP请求首行 获得请求方法 目标URL HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    
    // GET / HTTP/1.1
    m_url = strpbrk(text, " \t");
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/ HTTP/1.1
    *m_url++ = '\0';
    // method = GET  text = / HTTP/1.1
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    // /\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // 主状态机检查状态变为检查请求头    

    return NO_REQUEST;
}    

// 解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_len字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_len != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则 已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } 
    // 解析每一行是否为特殊头部字段
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        // strspn 在text中找到空格 返回其大小
        // text += 表示跳过该空格 剩余的就是对应的value
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_len = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}   

// 解析请求体 没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if ( m_read_idx >= ( m_content_len + m_checked_index ) )
    {
        text[ m_content_len ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}   

// 解析一行 判断依据为\r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    // 循环对每个字符进行处理
    for (; m_checked_index < m_read_idx; ++m_checked_index) {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r'){
            if (m_checked_index + 1 == m_read_idx){
                return LINE_OPEN;
            } else if ( m_read_buf[m_checked_index + 1] == '\n') {
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r')) {
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}       

// 对解析得到的HTTP请求进行相应处理
// 当得到一个完整、正确的HTTP请求时，分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    // /home/tortoise/Codes/MyWebServer/resourses
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 将解析到的URL与根目录拼接得到文件真实目录
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取 文件的相关状态信息 -1失败 0成功
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_REQUEST;
    }
    // printf("%s\n",m_real_file);
    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    // 文件请求成功
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作 释放内存映射
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

// 写HTTP响应 非阻塞的写 一次性写完数据
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节数
    int bytes_to_send = m_write_idx;    // 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数

    if (bytes_to_send == 0){
        // 没有将要发送到数据 响应结束
        modfd( m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间,则等待下一轮
            // 虽然在此期间 服务器无法立即接收同一客户的下一个请求，但可以保证连接的完整性
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if (m_linger) {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            } else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }    
}

// 回写响应 主函数
bool http_conn::process_write(HTTP_CODE read_ret){
    switch (read_ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            // 将 响应缓冲区 和 内存映射内容 组合封装到m_iv中
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }

    va_list arg_list;   // 解析参数
    va_start( arg_list, format );   // 初始化arg_list
    // vsnprintf 向一个字符串缓冲区打印格式化字符串
    // args ： 缓冲区写起始位置 可以写入的大小（-1原因是末尾还有\0） 格式 参数
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );

    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    
    return true;
}

// 添加响应状态行
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

// 添加响应状态头
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger(){
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line(){
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 由线程池中的工作线程调用 是处理HTTP请求的入口函数
void http_conn::process(){
    
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    printf("HTTP CODE : %d\n", read_ret);
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    

    // 生成HTTP响应
    bool write_ret = process_write(read_ret);
    if (!write_ret){
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}