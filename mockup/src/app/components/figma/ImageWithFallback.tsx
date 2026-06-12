import { useState } from "react";

interface ImageWithFallbackProps extends React.ImgHTMLAttributes<HTMLImageElement> {
  src: string;
  alt: string;
}

/**
 * Image component that gracefully handles load errors by hiding itself.
 */
export function ImageWithFallback({ src, alt, style, ...props }: ImageWithFallbackProps) {
  const [hasError, setHasError] = useState(false);

  if (hasError) return null;

  return (
    <img
      src={src}
      alt={alt}
      style={style}
      onError={() => setHasError(true)}
      {...props}
    />
  );
}
