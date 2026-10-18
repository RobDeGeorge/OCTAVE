/**
 * OCTAVE Python Wiki — Sidebar Navigation
 * Injects the navigation and the frozen-snapshot banner into every page and
 * highlights the current page.
 */
(function () {
  const sections = [
    {
      title: "Introduction",
      links: [
        { href: "index.html", label: "Home" },
        { href: "getting-started.html", label: "Getting Started" },
        { href: "architecture.html", label: "Architecture" },
      ],
    },
    {
      title: "Backend",
      links: [
        { href: "settings-manager.html", label: "Settings Manager" },
        { href: "media-manager.html", label: "Media Manager" },
        { href: "obd-manager.html", label: "OBD Manager" },
        { href: "network-manager.html", label: "Network Manager" },
        { href: "download-manager.html", label: "Download Manager" },
        { href: "hardware-managers.html", label: "Hardware Managers" },
        { href: "phone-mirror-android-auto.html", label: "Phone Mirror & AA" },
        { href: "audio-analyzer.html", label: "Audio Analyzer" },
        { href: "utilities.html", label: "Utilities" },
      ],
    },
    {
      title: "Frontend",
      links: [
        { href: "frontend-overview.html", label: "Frontend Overview" },
        { href: "pages.html", label: "Pages & Views" },
        { href: "bottom-bar.html", label: "Bottom Bar" },
        { href: "theme-system.html", label: "Theme System" },
        { href: "settings-ui.html", label: "Settings UI" },
        { href: "components.html", label: "Components" },
        { href: "gauges-dashboards.html", label: "Gauges & Dashboards" },
      ],
    },
    {
      title: "Reference & Guides",
      links: [
        { href: "settings-reference.html", label: "Settings Reference" },
        { href: "signals-slots-reference.html", label: "Signals & Slots API" },
        { href: "hardware-setup.html", label: "Hardware Setup" },
        { href: "building.html", label: "Running from Source" },
        { href: "development.html", label: "Development" },
        { href: "troubleshooting.html", label: "Troubleshooting" },
      ],
    },
  ];

  // Determine current page
  const currentPage = window.location.pathname.split("/").pop() || "index.html";

  // Build sidebar HTML
  const sidebar = document.createElement("nav");
  sidebar.className = "sidebar";
  sidebar.id = "sidebar";

  let html = `
    <div class="sidebar-header">
      <a href="index.html" class="sidebar-logo">
        OCTAVE <span class="logo-accent">python wiki</span>
      </a>
    </div>
  `;

  for (const section of sections) {
    html += `<div class="sidebar-section">`;
    html += `<div class="sidebar-section-title">${section.title}</div>`;
    for (const link of section.links) {
      const active = link.href === currentPage ? " active" : "";
      html += `<a href="${link.href}" class="sidebar-link${active}">${link.label}</a>`;
    }
    html += `</div>`;
  }

  sidebar.innerHTML = html;

  // Build mobile toggle
  const toggle = document.createElement("button");
  toggle.className = "sidebar-toggle";
  toggle.id = "sidebar-toggle";
  toggle.innerHTML = "&#9776;";
  toggle.setAttribute("aria-label", "Toggle navigation");
  toggle.addEventListener("click", function () {
    sidebar.classList.toggle("open");
  });

  // Close sidebar on link click (mobile)
  sidebar.addEventListener("click", function (e) {
    if (e.target.classList.contains("sidebar-link")) {
      sidebar.classList.remove("open");
    }
  });

  // Insert into DOM
  document.body.insertBefore(sidebar, document.body.firstChild);
  document.body.insertBefore(toggle, document.body.firstChild);

  // Frozen-snapshot banner at the top of every page
  function addBanner() {
    const inner = document.querySelector(".content-inner");
    if (!inner || document.getElementById("frozen-banner")) return;
    // index.html carries its own frozen-snapshot notice
    for (const t of inner.querySelectorAll(".info-box-title")) {
      if (t.textContent.startsWith("Frozen snapshot")) return;
    }
    const banner = document.createElement("div");
    banner.className = "info-box warning";
    banner.id = "frozen-banner";
    banner.innerHTML =
      '<div class="info-box-title">Frozen snapshot: Python / PySide6 backend</div>' +
      'This wiki documents the Python backend exactly as it stood when it was frozen on the ' +
      '<code>python-final</code> branch. It is not maintained and gets no fixes. Current OCTAVE ' +
      'is C++ / Qt 6 only, on the <code>main</code> branch, with its own wiki in <code>wiki/</code> there.';
    inner.insertBefore(banner, inner.firstChild);
  }
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", addBanner);
  } else {
    addBanner();
  }
})();
