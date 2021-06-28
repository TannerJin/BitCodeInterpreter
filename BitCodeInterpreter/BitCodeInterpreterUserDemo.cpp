//
//  BitCodeInterpreterUserDemo.hpp
//  BitCodeInterpreter
//
//  Created by jintao on 2021/6/28.
//

#ifndef BitCodeInterpreterUserDemo_hpp
#define BitCodeInterpreterUserDemo_hpp

#include <stdio.h>
#include "llvm-c/IRReader.h"
#include "IRReader/IRReader.h"
#include "SourceMgr.h"
#include "Module.h"
#include "LLVMContext.h"
#include "ExecutionEngine.h"

using namespace llvm;

extern "C" int InterpreterMain(const char * irFile, int argc, char **argv, char * const *envp);

int InterpreterMain(const char * irFile, int argc, char **argv, char * const *envp) {
    // 初始化IR文件
    SMDiagnostic Err;
    LLVMContext Context;
    std::unique_ptr<Module> Owner = parseIRFile(irFile, Err, Context);
    Module *Mod = Owner.get();
    
    if (Mod) {
//        Mod->dump();
    }
    
    // 初始化解释器
    EngineBuilder builder(std::move(Owner));
    builder.setEngineKind(EngineKind::Interpreter);
    
    std::unique_ptr<ExecutionEngine> EE(builder.create());


    // 初始化参数
    std::vector<std::string> inputArgv;
    if (argc > 0 && argv != NULL) {
        for (int i = 0; argv[i] != NULL; i++) {
            inputArgv.push_back(argv[i]);
        }
    }

    // Run
    Function *EntryFn = Mod->getFunction("main");
    EE->runFunctionAsMain(EntryFn, inputArgv, envp);
    
    return 0;
}

#endif /* BitCodeInterpreterUserDemo_hpp */
