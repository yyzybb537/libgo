package main

import(
	"runtime"
	"time"
	"fmt"
	"os"
	"strconv"
	"sync/atomic"
	"sync"
)

var nThreads int = 1

func Switch_1(n int) {
	c := make(chan bool)
	go func(){
		for i := 0; i < n; i++ {
			// switch
			runtime.Gosched()
        }

		c <- true
    }()

	<-c
}

func Switch_1000(n int) {
	nCoro := 1000
	var done int64 = 0
	c := make(chan bool, 1)
	for i := 0; i < nCoro; i++ {
		go func(){
			for i := 0; i < n / nCoro; i++ {
				// switch
				runtime.Gosched()
			}

			if atomic.AddInt64(&done, 1) == int64(nCoro) {
				c <- true
			}
		}()
	}

	<-c
}

func Channel(capacity int, n int) {
	q := make(chan bool, nThreads)
	c := make(chan bool, capacity)
	for i := 0; i < nThreads; i++ {
		go func(){
			for i := 0; i < n; i++ {
				c <- true
			}
			q <- true
		}()

		go func(){
			for i := 0; i < n; i++ {
				<-c
			}
			q <- true
		}()
	}

	for i := 0; i < 2 * nThreads; i++ {
		<-q
	}
}

func Mutex(n int) {
	var mtx sync.Mutex
	var val int64 = 0

	c := make(chan bool, nThreads)
	for i := 0; i < nThreads; i++ {
		go func(){
			for i := 0; i < n; i++ {
				mtx.Lock()
				val++
				mtx.Unlock()
			}
			 
			c <- true
		}()
	}

	for i := 0; i < nThreads; i++ {
		<-c
	}
}

func main() {
	var n int64 = 1000000
	var N int = int(n)

	if len(os.Args) > 1 {
		nThreads, _ = strconv.Atoi(os.Args[1])
	}

	runtime.GOMAXPROCS(nThreads)
	fmt.Printf("Thread: %d\n", runtime.GOMAXPROCS(0));

	t := time.Now()
	Switch_1(N)
	nanos := time.Since(t).Nanoseconds()
	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Switch_1", n, nanos/n, 100000 * n / nanos);

	t = time.Now()
	Switch_1000(N)
	nanos = time.Since(t).Nanoseconds()
	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Switch_1000", n, nanos/n, 100000 * n / nanos);

	n *= int64(nThreads)

//	t = time.Now()
//	Mutex(N)
//	nanos = time.Since(t).Nanoseconds()
//	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Mutex", n, nanos/n, 100000 * n / nanos);

	t = time.Now()
	Channel(0, N)
	nanos = time.Since(t).Nanoseconds()
	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Channel_0", n, nanos/n, 100000 * n / nanos);

	t = time.Now()
	Channel(1, N)
	nanos = time.Since(t).Nanoseconds()
	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Channel_1", n, nanos/n, 100000 * n / nanos);

	t = time.Now()
	Channel(10000, N)
	nanos = time.Since(t).Nanoseconds()
	fmt.Printf("%s    %d    %d ns/op    %d w/s\n", "Channel_10000", n, nanos/n, 100000 * n / nanos);
}
