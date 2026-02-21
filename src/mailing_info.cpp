#include "mailing_info.h"

#include "issues.h"

#include "html_utils.h"

#include <algorithm>
#include <format>
#include <istream>
#include <iterator>

namespace {

void replace_all_irefs(std::span<const lwg::issue> issues, std::string & s) {
   // Replace all tagged "issues references" in string 's' with an HTML anchor-link to the live issue
   // in its appropriate issue list, as determined by the issue's status.
   // Format of an issue reference: <iref ref="ISS"/>
   // Format of anchor: <a href="lwg-INDEX.html#ISS">ISS</a>

   for (auto i = s.find("<iref ref=\""); i != std::string::npos; i = s.find("<iref ref=\"")) {
      auto j = s.find('>', i);
      if (j == std::string::npos) {
         throw std::runtime_error{"missing '>' after iref"};
      }

      auto k = s.find('"', i+5);
      if (k >= j) {
         throw std::runtime_error{"missing '\"' in iref"};
      }
      auto l = s.find('"', k+1);
      if (l >= j) {
         throw std::runtime_error{"missing '\"' in iref"};
      }

      ++k;

      int num;

      try {
         num = lwg::stoi(s.substr(k, l-k));
      } catch (const std::exception&) {
         throw std::runtime_error{"bad number in iref: " + s.substr(k, l-k) };
      }

      auto n = std::ranges::lower_bound(issues, num, {}, &lwg::issue::num);
      if (n == issues.end() || n->num != num) {
         throw std::runtime_error{std::format("couldn't find issue number in <iref>: {}", num)};
      }

      std::string r{make_html_anchor(*n)};
      j -= i - 1;
      s.replace(i, j, r);
      // i += r.size() - 1;  // unused, copy/paste from elsewhere?
   }
}

} // close unnamed namespace

auto lwg::make_html_anchor(lwg::issue const & iss) -> std::string {
   auto num = std::to_string(iss.num);
   // Remove XML elements from the title to just get the text nodes.
   auto title = lwg::strip_xml_elements(iss.title);
   // Attribute text is delimited with quotes so replace them with "&quot;".
   title = lwg::replace_reserved_char(std::move(title), '"', "&quot;");

   return std::format("<a href=\"{0}#{1}\" title=\"{2} (Status: {3})\">{1}</a>",
       filename_for_status(iss.stat), num, title, iss.stat);
}

namespace lwg
{

mailing_info::mailing_info(std::istream & stream)
   : m_data{std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{}}
   {
}

auto mailing_info::get_doc_number(std::string doc) const -> std::string_view {
    if (doc == "active") {
        doc = "active_docno";
    }
    else if (doc == "defect") {
        doc = "defect_docno";
    }
    else if (doc == "closed") {
        doc = "closed_docno";
    }
    else {
        throw std::runtime_error{"unknown argument to get_doc_number: " + std::string{doc}};
    }

    return get_attribute(doc);
}

auto mailing_info::get_intro(std::string_view doc) const -> std::string_view {
    if (doc == "active") {
        doc = "<intro list=\"Active\">";
    }
    else if (doc == "defect") {
        doc = "<intro list=\"Defects\">";
    }
    else if (doc == "closed") {
        doc = "<intro list=\"Closed\">";
    }
    else {
        throw std::runtime_error{"unknown argument to intro: " + std::string{doc}};
    }

    auto i = m_data.find(doc);
    if (i == std::string::npos) {
        throw std::runtime_error{"Unable to find intro in lwg-issues.xml"};
    }
    i += doc.size();
    auto j = m_data.find("</intro>", i);
    if (j == std::string::npos) {
        throw std::runtime_error{"Unable to parse intro in lwg-issues.xml"};
    }
    return std::string_view{m_data}.substr(i, j-i);
}


auto mailing_info::get_maintainer() const -> std::string {
   std::string_view r = get_attribute("maintainer");
   auto m = r.find("&lt;");
   if (m == r.npos) {
      throw std::runtime_error{"Unable to parse maintainer email address in lwg-issues.xml"};
   }
   m += sizeof("&lt;") - 1;
   std::string_view pre = r.substr(0, m);
   auto me = r.find("&gt;", m);
   if (me == r.npos) {
      throw std::runtime_error{"Unable to parse maintainer email address in lwg-issues.xml"};
   }
   std::string_view post = r.substr(me);
   std::string_view email = r.substr(m, me-m);
   // Name &lt;                                    lwgchair@gmail.com    &gt;
   // Name &lt;<a href="mailto:lwgchair@gmail.com">lwgchair@gmail.com</a>&gt;
   return std::format("{0}<a href=\"mailto:{1}\">{1}</a>{2}", pre, email, post);
}

auto mailing_info::get_revision() const -> std::string_view {
   return get_attribute("revision");
}


auto mailing_info::get_revisions(std::span<const issue> issues, std::string const & diff_report) const -> std::string {
   auto i = m_data.find("<revision_history>");
   if (i == std::string::npos) {
      throw std::runtime_error{"Unable to find <revision_history> in lwg-issues.xml"};
   }
   i += sizeof("<revision_history>") - 1;

   auto j = m_data.find("</revision_history>", i);
   if (j == std::string::npos) {
      throw std::runtime_error{"Unable to find </revision_history> in lwg-issues.xml"};
   }
   auto s = std::string_view{m_data}.substr(i, j-i);
   j = 0;

   // We should date and *timestamp* this reference, as we expect to generate several documents per day
   std::string r = std::format("<ul>\n<li>{}: {} {}{}</li>\n",
       get_revision(), get_date(), get_title(), diff_report);

   while (true) {
      i = s.find("<revision tag=\"", j);
      if (i == s.npos) {
         break;
      }
      i += sizeof("<revision tag=\"") - 1;
      j = s.find('"', i);
      std::string_view rv = s.substr(i, j-i);
      i = j+2;
      j = s.find("</revision>", i);

      r += std::format("<li>{}: {}</li>\n", rv, s.substr(i, j-i));
   }
   r += "</ul>\n";

   replace_all_irefs(issues, r);

   return r;
}


auto mailing_info::get_statuses() const -> std::string_view {
   auto i = m_data.find("<statuses>");
   if (i == std::string::npos) {
      throw std::runtime_error{"Unable to find statuses in lwg-issues.xml"};
   }
   i += sizeof("<statuses>") - 1;

   auto j = m_data.find("</statuses>", i);
   if (j == std::string::npos) {
      throw std::runtime_error{"Unable to parse statuses in lwg-issues.xml"};
   }
   return std::string_view{m_data}.substr(i, j-i);
}

auto mailing_info::get_date() const -> std::string_view {
   return get_attribute("date");
}

auto mailing_info::get_title() const -> std::string_view {
   return get_attribute("title");
}

// TODO: this should be a generic routine for finding an attribute, usable in other files.
auto mailing_info::get_attribute(std::string_view attribute_name) const -> std::string_view {
    std::string attr{attribute_name};
    std::string search_string{" " + attr + "=\""};
    auto i = m_data.find(search_string);
    if (i == std::string::npos) {
        throw std::runtime_error{"Unable to find " + attr + " in lwg-issues.xml"};
    }
    i += search_string.size();
    auto j = m_data.find('"', i);
    if (j == std::string::npos) {
        throw std::runtime_error{"Unable to parse " + attr + " in lwg-issues.xml"};
    }
    return std::string_view{m_data}.substr(i, j-i);
}

} // close namespace lwg
