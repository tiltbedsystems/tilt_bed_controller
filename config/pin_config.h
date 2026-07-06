#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// $[CMU]
// [CMU]$

// $[LFXO]
// [LFXO]$

// $[PRS.ASYNCH0]
// [PRS.ASYNCH0]$

// $[PRS.ASYNCH1]
// [PRS.ASYNCH1]$

// $[PRS.ASYNCH2]
// [PRS.ASYNCH2]$

// $[PRS.ASYNCH3]
// [PRS.ASYNCH3]$

// $[PRS.ASYNCH4]
// [PRS.ASYNCH4]$

// $[PRS.ASYNCH5]
// [PRS.ASYNCH5]$

// $[PRS.ASYNCH6]
// [PRS.ASYNCH6]$

// $[PRS.ASYNCH7]
// [PRS.ASYNCH7]$

// $[PRS.ASYNCH8]
// [PRS.ASYNCH8]$

// $[PRS.ASYNCH9]
// [PRS.ASYNCH9]$

// $[PRS.ASYNCH10]
// [PRS.ASYNCH10]$

// $[PRS.ASYNCH11]
// [PRS.ASYNCH11]$

// $[PRS.SYNCH0]
// [PRS.SYNCH0]$

// $[PRS.SYNCH1]
// [PRS.SYNCH1]$

// $[PRS.SYNCH2]
// [PRS.SYNCH2]$

// $[PRS.SYNCH3]
// [PRS.SYNCH3]$

// $[GPIO]
// [GPIO]$

// $[TIMER0]
// TIMER0 CC0 on PB04
#ifndef TIMER0_CC0_PORT                         
#define TIMER0_CC0_PORT                          SL_GPIO_PORT_B
#endif
#ifndef TIMER0_CC0_PIN                          
#define TIMER0_CC0_PIN                           4
#endif

// [TIMER0]$

// $[TIMER1]
// TIMER1 CC0 on PB03
#ifndef TIMER1_CC0_PORT                         
#define TIMER1_CC0_PORT                          SL_GPIO_PORT_B
#endif
#ifndef TIMER1_CC0_PIN                          
#define TIMER1_CC0_PIN                           3
#endif

// [TIMER1]$

// $[TIMER2]
// TIMER2 CC0 on PB02
#ifndef TIMER2_CC0_PORT                         
#define TIMER2_CC0_PORT                          SL_GPIO_PORT_B
#endif
#ifndef TIMER2_CC0_PIN                          
#define TIMER2_CC0_PIN                           2
#endif

// [TIMER2]$

// $[TIMER3]
// TIMER3 CC0 on PC06
#ifndef TIMER3_CC0_PORT                         
#define TIMER3_CC0_PORT                          SL_GPIO_PORT_C
#endif
#ifndef TIMER3_CC0_PIN                          
#define TIMER3_CC0_PIN                           6
#endif

// [TIMER3]$

// $[TIMER4]
// [TIMER4]$

// $[USART0]
// USART0 RX on PB01
#ifndef USART0_RX_PORT                          
#define USART0_RX_PORT                           SL_GPIO_PORT_B
#endif
#ifndef USART0_RX_PIN                           
#define USART0_RX_PIN                            1
#endif

// USART0 TX on PB00
#ifndef USART0_TX_PORT                          
#define USART0_TX_PORT                           SL_GPIO_PORT_B
#endif
#ifndef USART0_TX_PIN                           
#define USART0_TX_PIN                            0
#endif

// [USART0]$

// $[USART1]
// [USART1]$

// $[I2C1]
// I2C1 SCL on PD02
#ifndef I2C1_SCL_PORT                           
#define I2C1_SCL_PORT                            SL_GPIO_PORT_D
#endif
#ifndef I2C1_SCL_PIN                            
#define I2C1_SCL_PIN                             2
#endif

// I2C1 SDA on PD03
#ifndef I2C1_SDA_PORT                           
#define I2C1_SDA_PORT                            SL_GPIO_PORT_D
#endif
#ifndef I2C1_SDA_PIN                            
#define I2C1_SDA_PIN                             3
#endif

// [I2C1]$

// $[PDM]
// [PDM]$

// $[LETIMER0]
// [LETIMER0]$

// $[IADC0]
// [IADC0]$

// $[I2C0]
// I2C0 SCL on PC03
#ifndef I2C0_SCL_PORT                           
#define I2C0_SCL_PORT                            SL_GPIO_PORT_C
#endif
#ifndef I2C0_SCL_PIN                            
#define I2C0_SCL_PIN                             3
#endif

// I2C0 SDA on PC02
#ifndef I2C0_SDA_PORT                           
#define I2C0_SDA_PORT                            SL_GPIO_PORT_C
#endif
#ifndef I2C0_SDA_PIN                            
#define I2C0_SDA_PIN                             2
#endif

// [I2C0]$

// $[EUART0]
// [EUART0]$

// $[PTI]
// [PTI]$

// $[MODEM]
// [MODEM]$

// $[CUSTOM_PIN_NAME]
#ifndef RST_EXP_N_PORT                          
#define RST_EXP_N_PORT                           SL_GPIO_PORT_A
#endif
#ifndef RST_EXP_N_PIN                           
#define RST_EXP_N_PIN                            0
#endif

#ifndef _PORT                                   
#define _PORT                                    SL_GPIO_PORT_A
#endif
#ifndef _PIN                                    
#define _PIN                                     1
#endif


#ifndef LOCAL_BUTTON_PORT                       
#define LOCAL_BUTTON_PORT                        SL_GPIO_PORT_A
#endif
#ifndef LOCAL_BUTTON_PIN                        
#define LOCAL_BUTTON_PIN                         3
#endif

#ifndef LOCAL_LED_PORT                          
#define LOCAL_LED_PORT                           SL_GPIO_PORT_A
#endif
#ifndef LOCAL_LED_PIN                           
#define LOCAL_LED_PIN                            4
#endif







#ifndef SPEED_MOTOR_3_PORT                      
#define SPEED_MOTOR_3_PORT                       SL_GPIO_PORT_B
#endif
#ifndef SPEED_MOTOR_3_PIN                       
#define SPEED_MOTOR_3_PIN                        2
#endif

#ifndef SPEED_MOTOR_2_PORT                      
#define SPEED_MOTOR_2_PORT                       SL_GPIO_PORT_B
#endif
#ifndef SPEED_MOTOR_2_PIN                       
#define SPEED_MOTOR_2_PIN                        3
#endif

#ifndef SPEED_MOTOR_1_PORT                      
#define SPEED_MOTOR_1_PORT                       SL_GPIO_PORT_B
#endif
#ifndef SPEED_MOTOR_1_PIN                       
#define SPEED_MOTOR_1_PIN                        4
#endif

#ifndef UI_INT_PORT                             
#define UI_INT_PORT                              SL_GPIO_PORT_C
#endif
#ifndef UI_INT_PIN                              
#define UI_INT_PIN                               0
#endif

#ifndef FAULT_INT_PORT                          
#define FAULT_INT_PORT                           SL_GPIO_PORT_C
#endif
#ifndef FAULT_INT_PIN                           
#define FAULT_INT_PIN                            1
#endif

#ifndef LOCAL_SDA_PORT                          
#define LOCAL_SDA_PORT                           SL_GPIO_PORT_C
#endif
#ifndef LOCAL_SDA_PIN                           
#define LOCAL_SDA_PIN                            2
#endif

#ifndef LOCAL_SCL_PORT                          
#define LOCAL_SCL_PORT                           SL_GPIO_PORT_C
#endif
#ifndef LOCAL_SCL_PIN                           
#define LOCAL_SCL_PIN                            3
#endif



#ifndef SPEED_MOTOR_4_PORT                      
#define SPEED_MOTOR_4_PORT                       SL_GPIO_PORT_C
#endif
#ifndef SPEED_MOTOR_4_PIN                       
#define SPEED_MOTOR_4_PIN                        6
#endif



#ifndef EXTERNAL_SCL_PORT                       
#define EXTERNAL_SCL_PORT                        SL_GPIO_PORT_D
#endif
#ifndef EXTERNAL_SCL_PIN                        
#define EXTERNAL_SCL_PIN                         2
#endif

#ifndef EXTERNAL_SDA_PORT                       
#define EXTERNAL_SDA_PORT                        SL_GPIO_PORT_D
#endif
#ifndef EXTERNAL_SDA_PIN                        
#define EXTERNAL_SDA_PIN                         3
#endif

// [CUSTOM_PIN_NAME]$


#endif // PIN_CONFIG_H


