DepartureMonoNerdFontMono-Regular.otf is the unmodified Mono variant from the
user-supplied DepartureMono Nerd Fonts download (Nerd Fonts 3.4.0, Departure Mono
1.422). It is the game's default UI and console font.

The downloaded font license is preserved in LICENSE.txt. Additional Nerd Fonts
licensing is retained in LICENSE-NERD-FONTS.txt.

The build embeds these original OTF bytes into the game and copies the font and
licenses to bin/assets/fonts/. The renderer bakes ASCII/Latin-1 glyphs into an R8
atlas using stb_truetype. No installed font or external runtime font path is needed.
