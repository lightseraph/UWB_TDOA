#ifndef  __kalman_H_
#define  __kalman_H_

//    卡尔曼滤波函数 （只适用于近似线性变化的量）
//     参数 ： 需要进行滤波的变量 

void kalman_filter_Init(void);
float kalman_filter(int kalman_val, int anc_num, int tag_num);

#endif 