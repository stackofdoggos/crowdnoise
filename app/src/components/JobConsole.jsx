import React, { useEffect, useRef } from "react";

export default function JobConsole({ lines }) {
  const ref = useRef(null);

  useEffect(() => {
    if (ref.current) ref.current.scrollTop = ref.current.scrollHeight;
  }, [lines]);

  if (!lines || !lines.length) return null;

  return (
    <div className="console" ref={ref}>
      {lines.map((l, i) => (
        <div key={i} className={l.startsWith("$") ? "cmd" : l.toLowerCase().includes("error") ? "err" : ""}>
          {l}
        </div>
      ))}
    </div>
  );
}
