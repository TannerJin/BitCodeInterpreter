# LLVM BitCode 解释器
  > 去除了 `JIT` & 只支持 `mach-o` 的 BitCode 解释器

## 用来干啥
  * Hotfix
  * VM Execution 

## 目前问题
  * 目前 Interpreter 文件依赖过多，正在优化
  
  * Bitcode 中自定义的`Objective-C`类，由于没有经过`objc_init`过，内存中不存在`class` 和 `SEL`, 计划通过`objc_registerClassPair` & `sel_registerName` 解决


