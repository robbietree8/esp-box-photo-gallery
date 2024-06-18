# ffmpeg -i input.jpg -vf scale=320:240 output.png

for i in *.jpg; do ffmpeg -i $i -vf scale="320:240" ${i}; done