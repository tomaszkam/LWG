#include "html_utils.h"
#include <format>

namespace lwg
{
// Find the offset of the first <elem> in xml.
std::string_view::size_type
find_element(std::string_view elem, std::string_view xml)
{
  std::string start_tag = std::format("<{}", elem);
  std::string_view::size_type pos = 0;
  while ((pos = xml.find(start_tag, pos)) != xml.npos)
  {
    if (char c = xml[pos + start_tag.size()]; c == '>' or c == ' ' or c == '/')
      return pos;
    pos += start_tag.size();
  }
  return pos;
}

// FIXME: This cannot handle "<p> <p>nested</p> </p>",
// it returns up to the first "</p>" instead of the second.
xml_element
get_element(std::string_view elem, std::string_view xml)
{
  // N.B. we assume no whitespace in end tag, i.e. "</elem>" not "</elem >"
  std::string end_tag = std::format("</{}>", elem);
  if (auto pos = find_element(elem, xml); pos != xml.npos)
  {
    xml.remove_prefix(pos);

    pos = xml.find('>');
    if (pos != xml.npos)
    {
      if (xml[pos - 1] == '/') // self-closing tag, e.g. "<p/>"
        return { xml.substr(0, pos + 1), {} };

      ++pos;
      auto pos2 = xml.find(end_tag, pos);
      if (pos2 != xml.npos)
        return { xml.substr(0, pos2 + end_tag.size()), xml.substr(pos, pos2 - pos) };
    }
  }
  throw std::runtime_error{"Element not found: " + std::string{elem}};
}

std::string_view
get_element_content(std::string_view elem, std::string_view xml)
{
  return lwg::get_element(elem, xml).inner;
}

std::string_view
get_attribute(std::string_view attr, std::string_view xml)
{
  std::string a = std::format(" {}=\"", attr);
  if (auto pos = xml.find(a); pos != xml.npos)
  {
    xml.remove_prefix(pos + a.size());
    pos = xml.find('"');
    if (pos != xml.npos)
      return xml.substr(0, pos);
  }
  throw std::runtime_error{"Attribute not found: " + std::string{attr}};
}

std::string_view
get_attribute_of(std::string_view attr, std::string_view elem, std::string_view xml)
{
  std::string_view::size_type pos = 0;
  while ((pos = find_element(elem, xml)) != xml.npos)
  {
    xml.remove_prefix(pos);
    auto substr = xml.substr(0, xml.find('>', elem.size() + 2) + 1);
    try {
      return get_attribute(attr, substr);
    } catch (const std::runtime_error&) {
      xml.remove_prefix(substr.size());
    }
  }
  throw std::runtime_error{std::format("Attribute of <{}> not found: {}", elem, attr)};
}

} // namespace lwg

#ifdef SELF_TEST
#include <cassert>
int main()
{
  std::string_view xml =
    "<xml>"
    "<elem>content <x/> ...</elem>"
    "<elt attr=\"foo\" attr2=\"bar\" />"
    "<elt attr3=\"three\" />"
    "<el attr=\"baz\">...</el>"
    "<p>para <p/>another para</p>"
    "<blockquote>quote <blockquote>nested quote</blockquote></blockquote>"
    "</xml>";
  assert(lwg::get_element("elem", xml).outer == "<elem>content <x/> ...</elem>");
  assert(lwg::get_element_content("elem", xml) == "content <x/> ...");
  assert(lwg::get_element("x", xml).outer == "<x/>");
  assert(lwg::get_element_content("x", xml) == "");
  assert(lwg::get_element("elt", xml).outer == "<elt attr=\"foo\" attr2=\"bar\" />");
  assert(lwg::get_element_content("elt", xml) == "");
  assert(lwg::get_element_content("p", xml) == "para <p/>another para");
  // FIXME: assert(lwg::get_element_content("blockquote", xml) == "quote <blockquote>nested quote</blockquote>");
  assert(lwg::get_attribute("attr", xml) == "foo");
  assert(lwg::get_attribute("attr3", xml) == "three");
  assert(lwg::get_attribute_of("attr3", "elt", xml) == "three");
  assert(lwg::get_attribute_of("attr", "el", xml) == "baz");
  try {
    lwg::get_attribute_of("attr2", "el", xml);
    assert(false);
  } catch (const std::runtime_error&) {
  }
}
#endif
