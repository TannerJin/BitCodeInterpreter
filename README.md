# LLVM BitCode 解释器
  > 去除了 `JIT` & 只支持 `mach-o` 的 BitCode 解释器

## 用来干啥
  * Hotfix
  * VM Execution 

## 目前发现问题
  * 目前 Interpreter 文件依赖过多，正在优化
  
  * Bitcode 中自定义的`Objective-C`类，由于没有经过`objc_init`过，`objc`中不存在对应`class`, 计划通过定制处理IR & `objc_registerClassPair` & `sel_registerName` 解决


## 注意
> 全局搜索 `Tanner` 查看定制化处理逻辑
