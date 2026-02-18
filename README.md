🖼 Gallery

The Pi Sieve running in portrait mode. The meditative scroll of primes against the "Time Running" header.
<img src="https://github.com/tliston/pi_sieve/blob/main/gallery/pi_sieve.jpg" alt="An image of the running pi_sieve installation." width="200" height="300">

🛠 Installation & Usage

    Clone the repository:
    Bash

    git clone https://github.com/yourusername/pi-sieve.git
    cd pi-sieve/base_prime_dump

    Compile base_prime_dump:

    make

    Run base prime dump to create the base_prime.bin file:

    ./base_prime_dump

    Move the base_prime.bin file into place:

    mv base_prime.bin ..

    (Note: the base_prime.bin file is processor independent. You'll likely want to create
    it on something besides a RasPi and move it over...)

    Compile prime_sieve for your architecture:
    The Makefile will automatically detect if you are on a Raspberry Pi (ARM Neon) or a Desktop (x64 SSE).
    Bash

    cd ..
    make

    Run the sieve:
    Bash

    ./pi_sieve

📜 Aesthetic Configuration

To achieve the "Artistic" look seen in the gallery:

    Font: Use Terminus at 16x32 for a bold, retro-digital look.

    Terminal: The code uses ANSI escape sequences to pin the header. Ensure your terminal supports DECSTBM (Standard on Linux Console/xterm).

    Color Palette: The header is set to White on Blue (\033[1;37;44m), providing a classic engineering-tool vibe.

💻 Software Architecture

The program is built in C99 and designed to be compiled with heavy optimizations. It saves progress, so if it is stopped (Ctrl-C), crashes, or the power dies, it'll restart where it left off.
