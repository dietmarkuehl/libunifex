# Index

* Receiver Queries
  * `get_stop_token()`
  * `get_scheduler()`
  * `get_allocator()`
* Sender Algorithms
  * `transform()`
  * `via()`
  * `typed_via()`
  * `on()`
  * `let()`
  * `sync_wait()`
  * `when_all()`
  * `with_query_value()`
  * `with_allocator()`
* Sender Types
  * `async_trace_sender`
* Sender Queries
  * `blocking()`
* Stream Algorithms
  * `adapt_stream()`
  * `next_adapt_stream()`
  * `reduce_stream()`
  * `for_each()`
  * `transform_stream()`
  * `via_stream()`
  * `typed_via_stream()`
  * `on_stream()`
  * `type_erase<Ts...>()`
  * `take_until()`
  * `single()`
  * `stop_immediately()`
* Stream Types
  * `range_stream`
  * `type_erased_stream<Ts...>`
  * `never_stream`
* StopToken Types
  * `unstoppable_token`
  * `inplace_stop_token` / `inplace_stop_source`

# Receiver Queries

### `cpo::get_scheduler(receiver)`

A query that can be used to obtain the associated scheduler from the receiver.

This can be used by senders to obtain a scheduler that can be used to schedule
work if required.

Receivers can customise this CPO to return the current scheduler.

See the `schedule()` algorithm, which schedules onto the current scheduler.

### `get_allocator(receiver)`

Obtain the current allocator that should be used for heap-allocating storage
needed by the implementation of a sender if required.

This may be customised by a receiver to return a specific allocator but if
it has not been customised then defaults to return `std::allocator<char>`.

### `get_stop_token(receiver)`

Obtain the current stop-token from the receiver.

If a sender's operation is able to be cancelled/interrupted then the sender should
call this function to query the stop-token provided by the receiver and use
this stop-token to either poll or subscribe for notification of a request to stop.

If a receiver has not customised this it will default to return `unstoppable_token`.

See the [Cancellation](cancellation.md) section for more details on cancellation.

# Sender Algorithms

### `transform(Sender predecessor, Func func) -> Sender`

Returns a sender that transforms the value of the `predecessor` by calling
`func(value)`.

### `via(Sender successor, Sender predecessor) -> Sender`

Returns a sender that produces the result from `predecessor` on the
execution context that `successor` completes on.

Any value produced by `successor` is discarded.
QUESTION: Should we require that `successor` is a `void`-sender?

If `successor` completes with `.done()` then `.done()` is sent.
If `successor` completes with `.error()` then its error is sent.
Otherwise sends the result of `predecessor`.

### `typed_via(Sender successor, Sender predecessor) -> Sender`

Returns a sender that produces the result from `predecessor`, which must
declare the nested `value_types`/`error_types` type aliases which describe which
overloads of `.value()`/`.error()` they will call, on the execution context that
`successor` completes on.

Any value produced by `successor` is discarded.

The semantics of `typed_via()` is the same as that of `via()` above but
avoids the need for heap allocation by pre-allocating storage for the
predecessor's result.

### `on(Sender predecessor, Sender successor) -> Sender`

Returns a sender that ensures that `successor` is started on the
execution context that `predecessor` completes on.

Discards any value produced by predecessor and sends the result of
`successor`. If `predecessor` completes with `.done()` or `.error()` then
sends that signal and never starts executing successor.

### `let(Sender pred, Invocable func) -> Sender`

The `let()` algorithm accepts a predecessor task that produces a value that
you want remain alive for the duration of a successor operation.

When the predecessor operation completes with a value, the function `func`
is invoked with lvalue references to copies of the values produced by
the predecessor. This invocation must return a Sender.

The references passed to `func` remain valid until the returned sender
completes, at which point the variables go out of scope.

For example:
```c++
let(some_operation(),
    [](auto& x) {
      return other_operation(x);
    });
```
is roughly equivalent to the following coroutine code:
```c++
{
  auto x = co_await some_operation();
  co_await other_operation(x);
}
```

If the predecessor completes with value then the `let()` operation as a
whole will complete with the result of the successor.

If the predecessor completes with done/error then `func` is not invoked
and the operation as a whole completes with that done/error signal.

### `sync_wait(Sender sender, StopToken st = {}) -> std::optional<Result>`

Blocks the current thread waiting for the specified sender to complete.

Returns a non-empty optional if it completed with `.value()`.
Or `std::nullopt` if it completed with `.done()`
Or throws an exception if it completed with `.error()`

### `when_all(Senders...) -> Sender`

Takes a variadic number of senders and returns a sender that launches each of
the input senders in-turn without waiting for the prior senders to complete.
This allows each of the input senders to potentially execute concurrently.

The result of the Sender has a value_type of:
`std::tuple<std::variant<std::tuple<Ts...>, ...>, ...>`

There is an element in the outer-tuple for each input sender.
Each element in the outer tuple is a variant that indicates which overload
of `value()` was called on the receiver by the corresponding sender.
The variant's value is a tuple that contains copies of the arguments passed
to `value()`.

If any of the input senders complete with done or error then it will request
any senders that have not yet completed to stop and the operation as a whole
will complete with done or error.

### `with_query_value(Sender sender, CPO cpo, T value) -> Sender`

Wraps `sender` in a new sender that will pass a receiver to `connect()`
on `sender` that customises CPO to return the specified value.

This can be used to inject contextual information into child operations.

For example:
```c++
inline constexpr unspecified get_some_property = {}; // Some CPO

sender auto some_async_operation() { ... }

sender auto inject_context() {
  // Inject the value '42' as the result of 'get_some_property()' when queried
  // by child operations of some_async_operation().
  return with_query_value(some_async_operation(), get_some_property, 42);
}
```

### `with_allocator(Sender sender, Allocator allocator) -> Allocator`

Wraps `sender` in a new sender that will injects `allocator` as the
result of `get_allocator()` query on receivers passed to child operations.

Child operations should use this allocator to perform heap allocations.

### `async_trace_sender`

A sender that will produce the current async stack-trace containing the
chain of continuations for the current async operation.

The stack-trace is represented as a `std::vector<async_trace_entry>` where
the `async_trace_entry` is defined as follows:

```
struct async_trace_entry {
  size_t depth; // depth of this trace entry from the starting point.
  size_t parentIndex; // index into vector of the parent continuation
  continuation_info continuation; // description of this continuation
};
```

### `cpo::blocking(const Sender&) -> blocking_kind`

Returns `blocking_kind::never` if the receiver will never be called on the
current thread before `start()` returns.

Returns `blocking_kind::always` if the receiver is guaranteed to be called
on some thread strongly-happens-before `start()` returns.
ie. the caller of `start()` can rely on the receiver having been called
after the `start()` method returns.

Returns `blocking_kind::always_inline` if the receiver is guaranteed to be
called inline on the current thread before `start()` returns.

Otherwise returns `blocking_kind::maybe`.

Senders can customise this algorithm by providing an overload of
`tag_invoke(tag_t<cpo::blocking>, const your_sender_type&)`.

## Stream Algorithms
-----------------
### `adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `stream.next()` and `stream.cleanup()` senders.

### `adapt_stream(Stream stream, Func nextAdaptor, Func cleanupAdaptor) -> Stream`

Applies `nextAdaptor()` to `stream.next()` and
applies `cleanupAdaptor()` to `stream.cleanup()`.

### `next_adapt_stream(Stream stream, Func adaptor) -> Stream`

Applies `adaptor()` to `stream.next()` only.
The `stream.cleanup()` Sender is passed through unchanged.

### `reduce_stream(Stream stream, T initialState, Func reducer) -> Sender<T>`

Applies `state = func(state, value)` for each value produced by `stream`.
Returns a Sender that returns the final value.

### `cpo::for_each(Stream stream, Func func) -> Sender<void>`

Executes func(value) for each value produced by stream.
Returned sender sends .value() once end of stream is reached.

Stream types can customise this algorithm via ADL by providing an overload
of `tag_invoke(tag_t<cpo::for_each>, your_stream_type, Func)`.

### `transform_stream(Stream stream, Func func) -> Stream`

Returns a stream that produces values that are the result of calling
`func(value)` on each value produced by the input stream.

### `via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified scheduler's
execution context.

Note that this works with streams that do not declare the types that they
send, but incurs a heap-allocation per value.

### `typed_via_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that calls the receiver methods on the specified
scheduler's execution context.

This differs from `via_stream()` in that it requires that the stream
declares what overloads of `.value()` and `.error()` it will call by
providing the `value_types`/`error_types` type aliases.

### `on_stream(Scheduler scheduler, Stream stream) -> Stream`

Returns a stream that ensures `stream.next()` is started on the specified
scheduler's execution context.

### `type_erase<Ts...>(Stream stream) -> type_erased_stream<Ts...>`

Type-erases the stream.
Stream must produce value packs of type `(Ts...,)`.

### `take_until(Stream source, Stream trigger) -> Stream`

Returns a stream that will produce values from 'source' until the 'trigger'
stream produces any of value/error/done.

### `single(Sender sender) -> Stream`

Returns a stream that will produce the result of `sender` as the result
of the first element of the stream. If this is a 'value' then it will
produce `done()` as the second element of the stream.

### `stop_immediately<Ts...>(Stream stream) -> Stream`

Returns a stream that will immediately send `.done()` from a pending `.next()`
when stop is requested on the provided stop-token.

The request to stop will be passed on to the upstream `.next()` call but
it will not wait for that stream to respond to cancellation before sending
`.done()`.

The abandoned `.next()` call will be waited-for by the `.cleanup()`.

Any `.value()` produced by an abandoned `.next()` call is discarded.
Any `.error()` produced by an abandoned `.next()` call is reported in
the `.cleanup()` result.

## Scheduler Algorithms

### `schedule(Scheduler schedule) -> SenderOf<void>`

This is the basis operation for a scheduler.

The `schedule` operation returns a sender that is a lazy async operation.

A schedule operation logically enqueues an item onto the scheduler's queue when `start()`
is called and the operation completes when some thread associated with the scheduler's
execution context dequeues that item.

The operation signals completion by invoking either the `set_value()`,
`set_done()` or `set_error()` methods on the receiver passed to `connect()`.

As the operation completes on the execution context, the `set_value()` method by definition
be called on that execution context. Applications can therefore use the `schedule()`
operation to execute logic on the associated execution context by placing that logic within
the body of `set_value()`.

### `schedule() -> SenderOf<void>`

This is like `schedule(scheduler)` above but uses the implicit scheduler
obtained from the receiver passed to `connect()` by a calling `get_scheduler(receiver)`.

### `delay(TimeScheduler scheduler, Duration d) -> Scheduler`

Adapts `scheduler` to produce a new scheduler that delays completion of all
`schedule()` operations by the specified duration.

## Scheduler Types

### `inline_scheduler`

The `schedule()` operation immediately invokes the receiver inline
upon calling `start()`.

### `single_thread_context`

Spawns a single background thread that executes tasks scheduled to it.

Call the `.get_scheduler()` method to obtain a scheduler that can be
used to schedule work to this thread.

### `trampoline_scheduler`

An inline scheduler that only allows invoking a maximum number of
operations inline recursively after which time it schedules subsequent
work to run once the call-stack has unwound back to the first call.

### `timed_single_thread_context`

A single-threaded execution context that suppors scheduling work at a
particular time via either `cpo::schedule_at()` with a time-point or
`cpo::schedule_after()` with a delay in addition to the regular `cpo::schedule()`
operation which is equivalent to calling `schedule_at()` with the current
time.

Obtain a TimeScheduler by calling the `.get_scheduler()` method.

### `thread_unsafe_event_loop`

An execution context that assumes all accesses to the scheduler are from the same
thread. It does not do any thread-synchronisation internally.

Supports `.schedule_at()` and `.schedule_after()` operations in addition to
the base `.schedule()` operation.

Obtain a TimeScheduler to schedule work onto this context by calling the
`.get_scheduler()` method.

### `linux::io_uring_context`

An I/O event loop execution context that makes use of the Linux io_uring APIs
to perform asynchronous file I/O.

You must call `.run()` from some thread to process tasks and I/O completions
posted to the I/O thread. Only a single call to `.run()` is allowed to execute
at a time.

The `.get_scheduler()` method returns a TimeScheduler object that can be used
to schedule work onto the I/O thread, using the `schedule()` or `schedule_at()`
CPOs.

You can also call one of the following CPOs, passing the scheduler obtained from
a given `io_uring_context`, to open a file:
* `open_file_read_only(scheduler, path) -> AsyncReadFile`
* `open_file_write_only(scheduler, path) -> AsyncWriteFile`
* `open_file_read_write(scheduler, path) -> AsyncReadWriteFile`

You can then use the following CPOs to read from and/or write to that file.
* `async_read_some_at(AsyncReadFile& file, AsyncReadFile::offset_t offset, span<std::byte> buffer)`
* `async_write_some_at(AsyncWriteFile& file, AsyncWriteFile::offset_t offset, span<const std::byte> buffer)`

These CPOs both return a `SenderOf<ssize_t>` that produces the number of bytes written.

For files associated with the `io_uring_context`, these operations will always complete
on the associated on the thread that is calling `run()` on the associated context.

## Stream Types

### `range_stream`

Produces a sequence of `int` values within a given range.
Mainly used for testing purposes.

### `type_erased_stream<Ts...>`

A type-erased stream that produces a sequence of value packs of type `(Ts, ...)`.
ie. calls to `.value()` will be passed arguments of type `Ts&&...`

### `never_stream`

A stream whose `.next()` completes with `.done()` once when stop is requested.

Note that using this stream with a stop-token where `stop_possible()` returns
`false` will result in a memory-leak. The `.next()` operation will never
complete.

## StopToken Types

### `unstoppable_token`

A trivial stop-token that can never be stopped.

This is used as the default stop-token for the `get_stop_token()`
customisation point.

### `inplace_stop_token` and `inplace_stop_source`

A stop token that can have stop requested via the corresponding
stop-source. The stop-token holds a reference to the stop-source
rather than heap-allocating some shared state. The caller must make
sure that all callbacks are deregistered and that any stop-tokens are
destroyed before the stop-source is destructed.

This is a less-safe but more efficient version of `std::stop_token`
proposed in [P0660R10](https://wg21.link/P0660R10).
