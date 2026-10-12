(() => {
  const top = document.querySelector(".top");
  const onScroll = () => {
    if (!top) return;
    top.classList.toggle("is-scrolled", window.scrollY > 24);
  };
  onScroll();
  window.addEventListener("scroll", onScroll, { passive: true });

  const io = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-in");
          io.unobserve(entry.target);
        }
      }
    },
    { rootMargin: "0px 0px -8% 0px", threshold: 0.12 }
  );
  document.querySelectorAll(".shot.reveal, .compare").forEach((el) => io.observe(el));
})();
