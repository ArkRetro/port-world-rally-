wpset 3a0000,20,w,1,{ printf "SYSW addr=%x data=%x pc=%x cyc=%d\n",wpaddr,wpdata,curpc,totalcycles ; g }
wpset 380000,10,w,1,{ printf "IOW addr=%x data=%x pc=%x\n",wpaddr,wpdata,curpc ; g }
g
