package main
import(
	"testing"
	"runtime"
)

func BenchmarkSwitch(b *testing.B) {
	c := make(chan bool)
	go func(){
		for i := 0; i < b.N; i++ {
			// switch
			runtime.Gosched()
        }

		c <- true
    }()

	<-c
}

func BenchmarkChannel_0(b *testing.B) {
	c := make(chan bool)
	go func(){
		for i := 0; i < b.N; i++ {
			c <- true
        }
    }()

	for i := 0; i < b.N; i++ {
		<-c
	}
}

func BenchmarkChannel_1(b *testing.B) {
	c := make(chan bool, 1)
	go func(){
		for i := 0; i < b.N; i++ {
			c <- true
        }
    }()

	for i := 0; i < b.N; i++ {
		<-c
	}
}

func BenchmarkChannel_N(b *testing.B) {
	c := make(chan bool, b.N)
	go func(){
		for i := 0; i < b.N; i++ {
			c <- true
        }
    }()

	for i := 0; i < b.N; i++ {
		<-c
	}
}
