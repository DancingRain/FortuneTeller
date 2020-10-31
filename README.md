# FortuneTeller
Animatronic Fortune Teller
Schematics and source code to accompany the YouTube Video: https://youtu.be/lZ59ihcLNIM

Motion, lights, and sounds are controlled vis UDP packets, as follows:

The first byte is the message type:  
'a': move arms, next two bytes are the angle in hex, 180° = down  0° = up  
'h': rotate head side to side, next two bytes are the angle in hex, 0° = right, 90° = forward, 180° = left  
'n': nod head up and down, next two bytes are the angle in hex, 0° = down, 90° = forward, 180° = up  
'm': combined motion: arms, then roation, then nod, each two bytes in hex.  
'c' and 'C': color, in hex, two digits each for red, green, and blue, just like HTML color values.  
  'c' fades gradually into the new color, 'C' changes color instantly.  
'Z': audio: one 512 sample frame of monaural, 16 bit, big-endian audio, sampled at 22050Hz  
  The fortune teller replies to this packet with 'Z' plus a single byte with the number of free audio buffers (0 - 8).  
  'Z' can be sent without audio samples to query the number of free buffers.  
    
OTA firmware updates have been implemented, because unplugging and replugging a serial cable was a pain in the neck.  

