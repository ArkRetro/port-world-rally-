wpset 0,100000,w,1,{ printf "CARTW addr=%x data=%x pc=%x\n",wpaddr,wpdata,curpc ; g }
wpset 200000,fff0,w,1,{ printf "CARTW addr=%x data=%x pc=%x\n",wpaddr,wpdata,curpc ; g }
wpset 2ffff0,10,w,1,{ printf "BANKW addr=%x data=%x\n",wpaddr,wpdata ; g }
g
