idf_component_register(
  SRCS "user_app.cpp"
  PRIV_REQUIRES  
  i2c_bsp 
  main 
  sdcard_bsp 
  adc_bsp 
  button_bsp  
  audio_bsp
  i2c_equipment
  INCLUDE_DIRS "./")
  
