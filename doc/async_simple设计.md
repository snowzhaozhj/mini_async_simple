# async_simple

## Try

Try包含三种状态：

* 什么都没有
* 包含一个值
* 包含一个异常

## SimpleExecutor

基于一个支持work steal的线程池，提供一个Schedule接口来提交任务，以及一个ScheduleById函数来根据id将任务提交到指定线程

## Future/Promise模型

### SharedState

SharedState由Promise和Future共同持有，由Promise在堆上构建，通过引用计数来管理自身的生命周期。

包含一个Try类型的Result和一个Continuation函数（使用std::function，以Try类型的值为参数）

SharedState包含4种状态：

* 刚刚构建
* 只设定了Result
* 只设定了Continuation函数
* 已经完成（即Result和Continuation函数都设置好了）

提供两个重要的函数：

* SetResult
* SetContinuation

当Result和Continuation都设置好之后，SharedState会将Result作为Continuation的参数打包成一个任务，通过Executor进行调度。如果没有Executor的话，或者Executor调度失败的话，会直接调用Continuation函数

### LocalState

LocalState仅由Future持有

包含一个Try类型的value（在构造LocalState的时候指定）

支持一个SetContinuation函数，会立即以Try类型的value为参数调用传入的Continuation函数

### Promise

在创建Promise的时候，会在堆上分配一个SharedState

逻辑比较简单，支持三个重要的函数：

* GetFuture：使用shared_state构造一个Future，并返回
* SetValue和SetException：都把逻辑直接托管给shared_state的SetResult函数

### Future

Future分为两种：

* 一种使用SharedState，由Promise间接构造，
* 一种使用LocalState，通过MakeReadyFuture函数构造

Future提供以下重要接口：

* Get函数：会调用Wait函数阻塞当前线程，直到Future成功获取到值，然后把值返回

* Wait函数：会创建一个新的Promise，并且为Promise的Future设置一个Continuation，改Continuation会将参数中的Try值存储到Promise中，并且会通知当前线程已经完成，当前线程则会等待Continuation的通知，原版使用mutex,condition_variable,atomic变量实现，我实现的版本采用std::binary_semaphore实现

* ThenTry和ThenValue: 这两个函数接受一个回调函数作为参数，这两个函数会创建一个新的Promise，并会在当前Future的Continuation函数中调用传入的回调函数，以及为新的Promise设置Result或Exception。最后将新的Promise的Future对象返回

  这也就意味着我们可以使用ThenTry或者ThenValue函数来串联一系列任务

## 无栈协程

### ReadAwaiter

是一个模板，构造函数接受一个值，await_ready函数设置成恒返回true，await_resume设置成返回构造函数传入的值

### ViaCoroutine

ViaAsyncAwaiter接受一个Awaitable，一个Executor，并且会存储Awaitable对应的Awaiter，executor，以及基于Executor构造一个新的ViaCoroutine

ViaAsyncAwaiter会将await_ready、await_suspend,await_resume等函数托管给传入的Awaitable对应的Awaiter，不过会在await_suspend的时候，会用ViaCoroutine包装一下传入的std::coroutine_handle，再传给awaiter的await_suspend函数

ViaCoroutine的包装过程：把传入的coroutine_handle设置成VIaCoroutine的continuation，并且将ViaCoroutine自身的coroutine_handle返回，同时会记录Executor的信息，如Executor Context

ViaCoroutine是一个简单的函数，只有一句`co_return`代码，在恢复ViaCoroutine之后，在执行`co_return`函数的时候，会先调用ViaCoroutine函数的promise的return_void函数，然后`co_await promise.final_suspend`，而final_suspend会返回一个FinalAwaiter，该FinalAwaiter的await_suspend函数会将ViaCoroutine的continuation的恢复Task传递给Executor来执行（会调度到之前的Context上），如果Executor为nullptr的话·，只需要简单的恢复continuation协程即可 

### LazyPromise

包含一个Executor指针，和一个coroutine_handle<>，命名为continuation

支持的await_transform函数:

1. 传入一个CurrentExecutor类，直接返回一个用ReadyAwaiter包装的executor指针即可
2. 传入一个Yield类，返回一个YieldAwaiter，YieldAwaiter会在await_suspend函数中，调用executor->Scheule接口提交一个任务，该任务的内容是恢复当前协程
3. 一个模板函数(不调用其他的重载则会调用这个模板函数)，传入一个Awaitable类型，然后会调用一个全局函数CoAwait(与关键字co_await不同)，该全局函数以executor和Awaitable为参数：
   * 如果Awaitable类型有一个成员函数CoAwait的话，则以executor为参数，调用该Awaitble类型的CoAwait函数
   * 如果不是的话，则会返回使用executor和Awaitable构造的ViaAsyncAwaiter

initial_suspend函数返回std::suspend_always

final_suspend函数会返回一个FinalAwaiter,FinalAwaiter的await_suspend函数会使用对称变换切换到LazyPromise的continuation协程

LazyPromise还包含一个值和异常的union，promise的return_value函数将会将union设置成值类型，unhandled_exception则会将union设置成异常类型

### LazyAwaiter

存储了一个Lazy对应协程的coroutine_handle

LazyAwaiter的await_suspend函数，会把传入的coroutine_handle设置成LazyPromise的continuation，并且恢复Lazy对应的协程。

LazyAwaiter的await_resume函数会尝试获取LazyPromise的结果（值或者异常），然后销毁LazyPromise的coroutine_handle

### Lazy

Lazy的co_await操作符会返回一个LazyAwaiter

Lazy提供以下重要函数：

* Start函数，接受一个回调函数作为参数，该函数会`co_await`等待当前Lazy任务执行完成，然后把结果作为参数调用回调函数
* Via函数：传入一个Executor，会生成一个RescheduledLazy，和Lazy只有细微的区别，即会将LazyAwaiter的awaiter的await_suspend的逻辑修改为使用指定的Executor来调度。

### CollectAll和CollectAny

实现一个CountEvent，CountEvent包含一个coroutine_handle和一个原子计数器，计数器进行反向的计数

CollectAllAwaiter的await_suspend函数中，等到原子计数为0，即所有任务执行完毕后，进行协程的resume，而CollectAnyAwaiter会在第一次计数器减的时候，进行协程的恢复

### FutureAwaiter

用于和Future/Promise集成，实现非常简单，只需要在await_suspend函数中调用future.SetContinuation设置一个函数就行，该函数会恢复传入的coroutine_handle

