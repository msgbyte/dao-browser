import type { AnchorHTMLAttributes } from 'react';
import styles from './Button.module.css';

type Variant = 'primary' | 'ghost';

export interface ButtonProps extends AnchorHTMLAttributes<HTMLAnchorElement> {
  variant?: Variant;
  fullWidth?: boolean;
  external?: boolean;
  disabled?: boolean;
}

export function Button({
  variant = 'primary',
  fullWidth = false,
  external = false,
  disabled = false,
  className,
  children,
  href,
  onClick,
  ...rest
}: ButtonProps) {
  const cls = [
    styles.base,
    styles[variant],
    fullWidth && styles.fullWidth,
    disabled && styles.disabled,
    className,
  ]
    .filter(Boolean)
    .join(' ');

  if (disabled) {
    // Render as a non-interactive span so the link cannot be activated by
    // click, keyboard, or assistive tech.
    return (
      <span className={cls} role="link" aria-disabled="true" {...rest}>
        {children}
      </span>
    );
  }

  const externalProps = external
    ? { target: '_blank', rel: 'noopener noreferrer' }
    : {};

  return (
    <a className={cls} href={href} onClick={onClick} {...externalProps} {...rest}>
      {children}
    </a>
  );
}
