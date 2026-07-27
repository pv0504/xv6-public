// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

static void
readline(char *buf, int sz)
{
  memset(buf, 0, sz);
  gets(buf, sz);

  int len = strlen(buf);
  if(len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = 0;
}

static int
authenticate(void)
{
  char username[100];
  char password[100];
  int attempts;

  for(attempts = 0; attempts < 3; attempts++) {

    printf(1, "Username: ");
    readline(username, sizeof(username));

    printf(1, "Password: ");
    readline(password, sizeof(password));

    if(strcmp(username, USERNAME) == 0 &&
       strcmp(password, PASSWORD) == 0) {
      printf(1, "Login successful\n");
      return 1;
    }

    printf(1, "Invalid credentials. Attempts left: %d\n",
           2 - attempts);
  }

  return 0;
}

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }

  dup(0);  // stdout
  dup(0);  // stderr

  if(!authenticate()){
    printf(1, "Maximum login attempts reached. Login disabled.\n");
    for(;;)
      sleep(100);
  }

  // Original xv6 shell loop
  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid = wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
