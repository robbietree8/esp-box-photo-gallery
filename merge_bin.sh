curr_day=$(date '+%Y-%m-%d')
cd build;esptool.py --chip esp32s3 merge_bin -o esp-box-photo-gallery-$curr_day.bin @flash_args
mv esp-box-photo-gallery-$curr_day.bin ../esp-box-photo-gallery-$curr_day.bin