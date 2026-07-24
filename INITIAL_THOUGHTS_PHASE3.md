# Initial thoughts
- No GUI obviously.
- But the parameters show up in the live 12 plugin good, 
- it doesn't crash live 12 when I add it to the audio channel. Good.
- Audio is making its way through the plugin. Good
- The feedback is maybe not enough? On max it doesn't really give that much of a thing, maybe it could be increased by 30%?
- Filter sounds pretty good tbh, and is working
- The pitch shifting is abit weak sounding doesn't seem to work that good.
- but whats odd is that you can still here the original  even when wet is all the way to 1, so I'd expect no original signal outputting 
- No gain parameter, eventually would be good to be able to control the gain of each filter, or atleast have basic input and output gain controlls. It peaks fairly easy, so I'm having to adjust the level of the synth feeding into it
- Limiter not sure if it's working, is it meant to clip or is it just softly making sure nothing exploades?

# Post fixes 10:00
- Still no pitch shift at all now
- Feedback is definitely higher but just screams as there's no effect other than filter/res on the signal
- hard to tell if wet/dry is working yet, but I suspect it might be.

# Post second fixes 10:17
- So the pitch shifting sounds WAY better, when pitching UP. 
- Still not obvious that wet / dry is working, but maybe it's because the wet still is meant to have some of the original signal still being preserved after processing.
- Pitch shifter is not pitching DOWN. 


# 20-07-2026 13:43 phase 3 fixes post pitch down test
I tested again after rebuilding and the following is apparent
- wet dry works for filtered sound, clearly hear the transition between filtered state and dry signal
- from 0 to 24 pitch up effect, but very choatic which is kind of cool
- same from 0 to -24 same up effect, both seem to do the same
- feedback is required to hear any effect, no pitch shift is heard when the feedback is 0
