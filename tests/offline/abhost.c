#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){void*h=dlopen(getenv("ABLIB"),RTLD_NOW); if(!h){printf("%s\n",dlerror());return 1;} return ((int(*)(int,char**))dlsym(h,"ab_main"))(c,v);}
