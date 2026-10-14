#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

/**
    The smallest PNG writer that works: RGB, deflate in stored blocks (no
    compression), so it needs no zlib. Only for --shots, where a few large files
    are no problem and being able to look at a frame is the whole point.
*/
namespace np::png
{
    inline std::uint32_t crc (const std::uint8_t* data, size_t n, std::uint32_t c = 0xFFFFFFFFu)
    {
        static std::uint32_t table[256];
        static bool ready = false;
        if (! ready)
        {
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t v = i;
                for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
                table[i] = v;
            }
            ready = true;
        }
        for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        return c;
    }

    inline bool writeRgb (const char* path, const std::uint8_t* rgb, int w, int h, int pitch)
    {
        std::vector<std::uint8_t> raw;
        raw.reserve ((size_t) (w * 3 + 1) * h);
        for (int y = 0; y < h; ++y)
        {
            raw.push_back (0);
            raw.insert (raw.end(), rgb + (size_t) y * pitch, rgb + (size_t) y * pitch + (size_t) w * 3);
        }

        std::vector<std::uint8_t> z { 0x78, 0x01 };
        std::uint32_t a = 1, b = 0;
        for (auto v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }

        for (size_t pos = 0; pos < raw.size(); )
        {
            const size_t n = std::min<size_t> (65535, raw.size() - pos);
            const bool last = pos + n == raw.size();
            z.push_back (last ? 1 : 0);
            z.push_back ((std::uint8_t) (n & 0xFF)); z.push_back ((std::uint8_t) (n >> 8));
            z.push_back ((std::uint8_t) (~n & 0xFF)); z.push_back ((std::uint8_t) ((~n >> 8) & 0xFF));
            z.insert (z.end(), raw.begin() + (long long) pos, raw.begin() + (long long) (pos + n));
            pos += n;
        }
        const std::uint32_t adler = (b << 16) | a;
        for (int s = 24; s >= 0; s -= 8) z.push_back ((std::uint8_t) (adler >> s));

        FILE* f = nullptr;
        if (fopen_s (&f, path, "wb") != 0 || ! f) return false;

        auto be32 = [f] (std::uint32_t v) { std::uint8_t q[4] { (std::uint8_t) (v >> 24), (std::uint8_t) (v >> 16), (std::uint8_t) (v >> 8), (std::uint8_t) v }; std::fwrite (q, 1, 4, f); };
        auto chunk = [&] (const char* type, const std::uint8_t* data, size_t n)
        {
            be32 ((std::uint32_t) n);
            std::fwrite (type, 1, 4, f);
            if (n) std::fwrite (data, 1, n, f);
            std::uint32_t c = crc ((const std::uint8_t*) type, 4);
            c = crc (data, n, c);
            be32 (c ^ 0xFFFFFFFFu);
        };

        static const std::uint8_t sig[8] { 137, 80, 78, 71, 13, 10, 26, 10 };
        std::fwrite (sig, 1, 8, f);

        std::uint8_t ihdr[13] { (std::uint8_t) (w >> 24), (std::uint8_t) (w >> 16), (std::uint8_t) (w >> 8), (std::uint8_t) w,
                                (std::uint8_t) (h >> 24), (std::uint8_t) (h >> 16), (std::uint8_t) (h >> 8), (std::uint8_t) h,
                                8, 2, 0, 0, 0 };
        chunk ("IHDR", ihdr, 13);
        chunk ("IDAT", z.data(), z.size());
        chunk ("IEND", nullptr, 0);

        std::fclose (f);
        return true;
    }
}
