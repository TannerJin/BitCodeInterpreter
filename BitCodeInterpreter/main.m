//
//  main.m
//  BitCodeInterpreter
//
//  Created by jintao on 2021/6/24.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "dlfcn.h"

extern int InterpreterMain(const char * irFile, int argc, char **argv, char * const *envp);

int main(int argc, char * argv[]) {
    NSString * appDelegateClassName;
    @autoreleasepool {
        // Setup code that might create autoreleased objects goes here.
        appDelegateClassName = NSStringFromClass([AppDelegate class]);
    }
    
    const char *path = [[NSString stringWithFormat:@"%@%s", NSBundle.mainBundle.bundlePath, "/main.bc"] cStringUsingEncoding:NSUTF8StringEncoding];
    printf("IR Path is: %s \n", path);
    
    // 开始执行 BitCode
    InterpreterMain(path, 0, NULL, NULL);
    
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
