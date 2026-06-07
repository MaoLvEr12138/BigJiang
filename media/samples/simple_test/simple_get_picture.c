#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int main() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // 必须和主程序中的 CMD_SOCKET_PATH 一致
    strcpy(addr.sun_path, "/tmp/camera_cmd.sock");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Connect fail (is the streamer running?)");
        return 1;
    }

    // 发送拍照指令
    write(fd, "snap", 4);

    // 接收回传的 JPEG 图像
    FILE *fp = fopen("/tmp/captured.jpg", "wb");
    char buf[4096];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, fp);
    }
    
    fclose(fp);
    close(fd);
    printf("Snapshot saved to /tmp/captured.jpg\n");
    return 0;
}
