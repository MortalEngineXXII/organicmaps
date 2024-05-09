#include "testing/testing.hpp"

#include "drape/drape_tests/img.hpp"

#include "drape/bidi.hpp"
#include "drape/glyph_manager.hpp"
#include "drape/harfbuzz_shape.hpp"

#include "platform/platform.hpp"

#include "coding/string_utf8_multilang.hpp"

#include <QtGui/QPainter>

#include "qt_tstfrm/test_main_loop.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <harfbuzz/hb-ft.h>
#include <harfbuzz/hb.h>

namespace glyph_mng_tests
{
std::ostream& operator<<(std::ostream & stream, const text_shape::TextMetrics & tm)
{
  stream << "width=" << tm.m_width << " [\n";
  for (auto const & gm : tm.m_glyphs)
    stream << "font=" << gm.m_font << ", id=" << gm.m_glyphId << ", xo=" << gm.m_xOffset << ", yo=" << gm.m_yOffset << ", xa=" << gm.m_xAdvance << '\n';
  stream << ']';
  return stream;
}
class GlyphRenderer
{
  FT_Library m_freetypeLibrary;
  strings::UniString m_bidiToDraw;
  std::string m_utf8;
  int m_fontPixelSize;
  char const * m_lang;

  static constexpr FT_Int kSdfSpread {dp::kSdfBorder};

public:
  GlyphRenderer()
  {
    // Initialize FreeType
    TEST_EQUAL(0, FT_Init_FreeType(&m_freetypeLibrary), ("Can't initialize FreeType"));
    for (auto const module : {"sdf", "bsdf"})
      TEST_EQUAL(0, FT_Property_Set(m_freetypeLibrary, module, "spread", &kSdfSpread), ());

    dp::GlyphManager::Params args;
    args.m_uniBlocks = "unicode_blocks.txt";
    args.m_whitelist = "fonts_whitelist.txt";
    args.m_blacklist = "fonts_blacklist.txt";
    GetPlatform().GetFontNames(args.m_fonts);

    m_mng = std::make_unique<dp::GlyphManager>(args);
  }

  ~GlyphRenderer()
  {
    FT_Done_FreeType(m_freetypeLibrary);
  }

  void SetString(std::string const & s, int fontPixelSize, char const * lang)
  {
    m_bidiToDraw = bidi::log2vis(strings::MakeUniString(s));
    m_utf8 = s;
    m_fontPixelSize = fontPixelSize;
    m_lang = lang;
  }

  static float Smoothstep(float edge0, float edge1, float x)
  {
    x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
  }

  static float PixelColorFromDistance(float distance)
  {
    //float const normalizedDistance = (distance - 128.f) / 128.f;
    float const normalizedDistance = distance / 255.f;
    static constexpr float kFontScale = 1.f;
    static constexpr float kSmoothing = 0.25f / (kSdfSpread * kFontScale);
    float const alpha = Smoothstep(0.5f - kSmoothing, 0.5f + kSmoothing, normalizedDistance);
    return 255.f * alpha;
  }

  void RenderGlyphs(QPaintDevice * device)
  {
    QPainter painter(device);
    painter.fillRect(QRectF(0.0, 0.0, device->width(), device->height()), Qt::white);

    text_shape::TextMetrics const metrics = text_shape::ShapeText(m_utf8, m_fontPixelSize, m_lang, [&](auto c, auto buf, auto height, auto & out)
    {
      m_mng->ShapeText(c, buf, height, out);
    });

    QPoint hbPen(10, 100);

    auto const hbLanguage = hb_language_from_string(m_lang, -1);

    auto runs = text_shape::ItemizeText(m_utf8);
    text_shape::ReorderRTL(runs);
    for (auto const& substring : runs.substrings)
    {
      hb_buffer_t *buf = hb_buffer_create();
      hb_buffer_add_utf16(buf, reinterpret_cast<const uint16_t *>(runs.text.data()), runs.text.size(), substring.m_start, substring.m_length);
      //hb_buffer_add_utf16(buf, reinterpret_cast<const uint16_t *>(sv.data()), substring.m_length, 0, substring.m_length);
      // If you know the direction, script, and language
      hb_buffer_set_direction(buf, substring.m_direction);
      hb_buffer_set_script(buf, substring.m_script);
      hb_buffer_set_language(buf, hbLanguage);

      // If direction, script, and language are not known.
      // hb_buffer_guess_segment_properties(buf);

      std::u16string_view const sv{runs.text.data() + substring.m_start, static_cast<size_t>(substring.m_length)};
      //auto const fontIndex = m_mng->GetFontIndex(sv);

      std::string lang = m_lang;
      std::string fontFileName = lang == "ar" ? "00_NotoNaskhArabic-Regular.ttf" : "07_roboto_medium.ttf";
      // "00_NotoNaskhArabic-Regular.ttf"
      //auto reader = GetPlatform().GetReader("06_code2000.ttf");
      auto reader = GetPlatform().GetReader(fontFileName);
      auto fontFile = reader->GetName();
      FT_Face face;
      if (FT_New_Face(m_freetypeLibrary, fontFile.c_str(), 0, &face)) {
        std::cerr << "Can't load font " << fontFile << '\n';
        return;
      }
      // Set character size
      FT_Set_Pixel_Sizes(face, 0 , m_fontPixelSize );
      // This also works.
      // if (FT_Set_Char_Size(face, 0, m_fontPixelSize << 6, 0, 0)) {
      //   std::cerr << "Can't set character size\n";
      //   return;
      // }

      // Set no transform (identity)
      //FT_Set_Transform(face, nullptr, nullptr);

      // Load font into HarfBuzz
      hb_font_t *font = hb_ft_font_create(face, nullptr);

      // Shape!
      hb_shape(font, buf, nullptr, 0);

      // Get the glyph and position information.
      unsigned int glyph_count;
      hb_glyph_info_t *glyph_info    = hb_buffer_get_glyph_infos(buf, &glyph_count);
      hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

      for (unsigned int i = 0; i < glyph_count; i++)
      {
        hb_codepoint_t const glyphid = glyph_info[i].codepoint;

        FT_Int32 const flags =  FT_LOAD_RENDER;
        FT_Load_Glyph(face, glyphid, flags);
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);

        FT_GlyphSlot slot = face->glyph;

        FT_Bitmap const & ftBitmap = slot->bitmap;

        auto const buffer = ftBitmap.buffer;
        auto const width = ftBitmap.width;
        auto const height = ftBitmap.rows;

        for (unsigned h = 0; h < height; ++h)
        {
          for (unsigned w = 0; w < width; ++w)
          {
           auto curPixelAddr = buffer + h * width + w;
           float currPixel = *curPixelAddr;
           //float sd = (currPixel - 128.f) / spread;
           // float sd = currPixel - 128.0f;
           // // Convert to pixel values.
           // sd = ( sd / 128.0f ) * spread;
           // // Store `sd` in a buffer or use as required.
            currPixel = PixelColorFromDistance(currPixel);
           //currPixel = currPixel - 128.f <= -2 ? 0 : 255;
           *curPixelAddr = (unsigned char)currPixel;
          }
        }

        auto const bearing_x = slot->metrics.horiBearingX;//slot->bitmap_left;
        auto const bearing_y = slot->metrics.horiBearingY;//slot->bitmap_top;

        auto const & glyphPos = glyph_pos[i];
        // hb_position_t const x_offset  = (glyphPos.x_offset + bearing_x) >> 6;
        // hb_position_t const y_offset  = (glyphPos.y_offset + bearing_y) >> 6;
        hb_position_t const x_offset  = (glyphPos.x_offset) >> 6;
        hb_position_t const y_offset  = (glyphPos.y_offset) >> 6;
        hb_position_t const x_advance = glyphPos.x_advance >> 6;
        hb_position_t const y_advance = glyphPos.y_advance >> 6;

        // Empty images are possible for space characters.
        if (width != 0 && height != 0)
        {
          QPoint currentPen = hbPen;
          currentPen.rx() += x_offset;
          currentPen.ry() -= y_offset;
          painter.drawImage(currentPen, CreateImage(width, height, buffer), QRect(kSdfSpread, kSdfSpread, width - 2*kSdfSpread, height - 2*kSdfSpread));
        }
        hbPen += QPoint(x_advance, y_advance);
      }

      // Tidy up.
      hb_buffer_destroy(buf);
      hb_font_destroy(font);
      FT_Done_Face(face);
    }

    //////////////////////////////////////////////////////////////////
    // QT text renderer.
    {
      QPoint pen(10, 150);
      //QFont font("Noto Naskh Arabic");
      QFont font("Roboto");
      font.setPixelSize(m_fontPixelSize);
      //font.setWeight(QFont::Weight::Normal);
      painter.setFont(font);
      painter.drawText(pen, QString::fromUtf8(m_utf8.c_str(), m_utf8.size()));
    }

    //////////////////////////////////////////////////////////////////
    // Old drape renderer.
    QPoint pen(10, 200);
    for (auto c : m_bidiToDraw)
    {
      auto g = m_mng->GetGlyph(c);

      if (g.m_image.m_data)
      {
        uint8_t * d = SharedBufferManager::GetRawPointer(g.m_image.m_data);
        QPoint currentPen = pen;
        currentPen.rx() += g.m_metrics.m_xOffset;
        currentPen.ry() -= g.m_metrics.m_yOffset;
        painter.drawImage(currentPen, CreateImage(g.m_image.m_width, g.m_image.m_height, d),
                          QRect(kSdfBorder, kSdfBorder, g.m_image.m_width - 2*kSdfBorder, g.m_image.m_height - 2*kSdfBorder));
      }
      pen += QPoint(g.m_metrics.m_xAdvance,  g.m_metrics.m_yAdvance);

      g.m_image.Destroy();
    }
  }

private:
  std::unique_ptr<dp::GlyphManager> m_mng;
};

// This unit test creates a window so can't be run in GUI-less Linux machine.
// Make sure that the QT_QPA_PLATFORM=offscreen environment variable is set.
UNIT_TEST(GlyphLoadingTest)
{
  GlyphRenderer renderer;

  using namespace std::placeholders;

  constexpr int fontSize = 54;

  renderer.SetString("Тестовая строка", fontSize, "ru");
  RunTestLoop("Test1", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));


//  renderer.SetString("ØŒÆ");
//  RunTestLoop("Test1", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

  //renderer.SetString("الحلّة گلها");
  renderer.SetString("الحلّة گلها"" كسول الزنجبيل القط""56""عين علي (الحربية)""123"" اَلْعَرَبِيَّةُ", fontSize, "ar");
  RunTestLoop("Test2", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

  // renderer.SetString("12345""گُلها""12345""گُلها""12345", 27, "ar");
  // RunTestLoop("Test3", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));
//
//  renderer.SetString("മനക്കലപ്പടി");
//  RunTestLoop("Test4", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

  // renderer.SetString("Test 12 345 ""گُلها""678 9000 Test", fontSize, "ar");
  // RunTestLoop("Test5", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));

  renderer.SetString("NFKC Razdoĺny NFKD Razdoĺny", fontSize, "be");
  RunTestLoop("Test5", std::bind(&GlyphRenderer::RenderGlyphs, &renderer, _1));
}

}  // namespace glyph_mng_tests
